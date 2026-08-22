// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/compositor_opengl.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <cmath>

#include "flutter/shell/platform/windows/egl/egl.h"
#include "GLES3/gl3.h"
#include "flutter/shell/platform/windows/flutter_windows_engine.h"
#include "flutter/shell/platform/windows/flutter_windows_view.h"

namespace flutter {

namespace {

constexpr uint32_t kWindowFrameBufferId = 0;

// Starling: present statistics behind STARLING_PRESENT_LOG=1 — one stderr
// line per presented frame with the swap duration and the gap since the
// previous present, plus the raw QPC timestamp so external tooling
// (Stopwatch.GetTimestamp reads the same counter) can correlate input
// timestamps with presents. This is the raster-thread half of the frame
// timing whose UI-thread half is the framework's STARLING_FRAME_LOG.
bool PresentLogEnabled() {
  static const bool enabled = [] {
    const char* value = getenv("STARLING_PRESENT_LOG");
    return value != nullptr && strcmp(value, "1") == 0;
  }();
  return enabled;
}

void PresentLogWrite(const char* kind, int64_t swap_start_qpc) {
  LARGE_INTEGER now, frequency;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&frequency);
  // Present runs on the single raster thread; plain statics suffice.
  static int64_t last_qpc = 0;
  int64_t swap_us =
      (now.QuadPart - swap_start_qpc) * 1000000 / frequency.QuadPart;
  int64_t gap_us =
      last_qpc != 0 ? (now.QuadPart - last_qpc) * 1000000 / frequency.QuadPart
                    : 0;
  last_qpc = now.QuadPart;
  fprintf(stderr, "[present] kind=%s qpc=%lld swap_us=%lld gap_us=%lld\n",
          kind, static_cast<long long>(now.QuadPart),
          static_cast<long long>(swap_us), static_cast<long long>(gap_us));
  fflush(stderr);
}

int64_t PresentLogNow() {
  if (!PresentLogEnabled()) {
    return 0;
  }
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return now.QuadPart;
}

// The metadata for an OpenGL framebuffer backing store.
struct FramebufferBackingStore {
  uint32_t framebuffer_id;
  uint32_t texture_id;
};

typedef const impeller::GLProc<decltype(glBlitFramebuffer)> BlitFramebufferProc;

const BlitFramebufferProc& GetBlitFramebufferProc(
    const impeller::ProcTableGLES& gl) {
  if (gl.BlitFramebuffer.IsAvailable()) {
    return gl.BlitFramebuffer;
  } else if (gl.BlitFramebufferANGLE.IsAvailable()) {
    return gl.BlitFramebufferANGLE;
  }

  // CompositorOpenGL::Initialize verifies that a blit procedure is available.
  FML_UNREACHABLE();
}

}  // namespace

CompositorOpenGL::CompositorOpenGL(FlutterWindowsEngine* engine,
                                   impeller::ProcTableGLES::Resolver resolver,
                                   bool enable_impeller)
    : engine_(engine), resolver_(resolver), enable_impeller_(enable_impeller) {}

bool CompositorOpenGL::CreateBackingStore(
    const FlutterBackingStoreConfig& config,
    FlutterBackingStore* result) {
  if (!is_initialized_ && !Initialize()) {
    return false;
  }

  auto store = std::make_unique<FramebufferBackingStore>();

  gl_->GenTextures(1, &store->texture_id);
  gl_->GenFramebuffers(1, &store->framebuffer_id);

  gl_->BindFramebuffer(GL_FRAMEBUFFER, store->framebuffer_id);

  gl_->BindTexture(GL_TEXTURE_2D, store->texture_id);
  gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl_->TexImage2D(GL_TEXTURE_2D, 0, format_.general_format, config.size.width,
                  config.size.height, 0, format_.general_format,
                  GL_UNSIGNED_BYTE, nullptr);
  gl_->BindTexture(GL_TEXTURE_2D, 0);

  if (enable_impeller_) {
    // Impeller requries that its onscreen surface is Multisampled and already
    // has depth/stencil attached in order for anti-aliasing to work.
    gl_->FramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER,
                                            GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                            store->texture_id, 0, 4);

    // Set up depth/stencil attachment for impeller renderer.
    GLuint depth_stencil;
    gl_->GenRenderbuffers(1, &depth_stencil);
    gl_->BindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
    gl_->RenderbufferStorageMultisampleEXT(
        GL_RENDERBUFFER,      // target
        4,                    // samples
        GL_DEPTH24_STENCIL8,  // internal format
        config.size.width,    // width
        config.size.height    // height
    );
    gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_RENDERBUFFER, depth_stencil);
    gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                 GL_RENDERBUFFER, depth_stencil);

  } else {
    gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, store->texture_id, 0);
  }

  result->type = kFlutterBackingStoreTypeOpenGL;
  result->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  result->open_gl.framebuffer.name = store->framebuffer_id;
  result->open_gl.framebuffer.target = format_.sized_format;
  result->open_gl.framebuffer.user_data = store.release();
  result->open_gl.framebuffer.destruction_callback = [](void* user_data) {
    // Backing store destroyed in `CompositorOpenGL::CollectBackingStore`, set
    // on FlutterCompositor.collect_backing_store_callback during engine start.
  };
  return true;
}

bool CompositorOpenGL::CollectBackingStore(const FlutterBackingStore* store) {
  FML_DCHECK(is_initialized_);
  FML_DCHECK(store->type == kFlutterBackingStoreTypeOpenGL);
  FML_DCHECK(store->open_gl.type == kFlutterOpenGLTargetTypeFramebuffer);

  auto user_data = static_cast<FramebufferBackingStore*>(
      store->open_gl.framebuffer.user_data);

  gl_->DeleteFramebuffers(1, &user_data->framebuffer_id);
  gl_->DeleteTextures(1, &user_data->texture_id);

  delete user_data;
  return true;
}

bool CompositorOpenGL::Present(FlutterWindowsView* view,
                               const FlutterLayer** layers,
                               size_t layers_count) {
  FML_DCHECK(view != nullptr);

  // Clear the view if there are no layers to present.
  if (layers_count == 0) {
    // Normally the compositor is initialized when the first backing store is
    // created. However, on an empty frame no backing stores are created and
    // the present needs to initialize the compositor.
    if (!is_initialized_ && !Initialize()) {
      return false;
    }

    return Clear(view);
  }

  // TODO: Support compositing layers and platform views.
  // See: https://github.com/flutter/flutter/issues/31713
  FML_DCHECK(is_initialized_);
  FML_DCHECK(layers_count == 1);
  FML_DCHECK(layers[0]->offset.x == 0 && layers[0]->offset.y == 0);
  FML_DCHECK(layers[0]->type == kFlutterLayerContentTypeBackingStore);
  FML_DCHECK(layers[0]->backing_store->type == kFlutterBackingStoreTypeOpenGL);
  FML_DCHECK(layers[0]->backing_store->open_gl.type ==
             kFlutterOpenGLTargetTypeFramebuffer);

  auto width = layers[0]->size.width;
  auto height = layers[0]->size.height;

  // Check if this frame can be presented. This resizes the surface if a resize
  // is pending and |width| and |height| match the target size.
  if (!view->OnFrameGenerated(width, height)) {
    return false;
  }

  // |OnFrameGenerated| should return false if the surface isn't valid.
  FML_DCHECK(view->surface() != nullptr);
  FML_DCHECK(view->surface()->IsValid());

  egl::WindowSurface* surface = view->surface();
  if (!surface->MakeCurrent()) {
    return false;
  }

  auto source_id = layers[0]->backing_store->open_gl.framebuffer.name;

  // STARLING partial present: when the engine reports this frame's damage
  // and the EGL window surface preserves its back buffer across swaps
  // (ANGLE on D3D11 renders the GL back buffer into a stable offscreen
  // texture, so EGL_BUFFER_PRESERVED is honored), blit only the damaged
  // rect. The first preserved frame still blits fully - the back buffer's
  // prior content predates preservation. Any doubt falls back to the full
  // blit.
  int blit_x0 = 0;
  int blit_y0 = 0;
  int blit_x1 = width;
  int blit_y1 = height;
  const FlutterBackingStorePresentInfo* present_info =
      layers[0]->backing_store_present_info;
  if (present_info != nullptr && present_info->frame_damage != nullptr &&
      present_info->frame_damage->rects_count == 1) {
    const FlutterRect& damage = present_info->frame_damage->rects[0];
    EGLDisplay egl_display = eglGetCurrentDisplay();
    const EGLSurface& egl_surface = surface->GetHandle();
    eglSurfaceAttrib(egl_display, egl_surface, EGL_SWAP_BEHAVIOR,
                     EGL_BUFFER_PRESERVED);
    EGLint behavior = 0;
    if (eglQuerySurface(egl_display, egl_surface, EGL_SWAP_BEHAVIOR,
                        &behavior) == EGL_TRUE &&
        behavior == EGL_BUFFER_PRESERVED) {
      if (preserved_surfaces_.count(surface) > 0) {
        // Damage arrives in top-left device coordinates; the GL blit space
        // for these framebuffers is bottom-left. Same rect on both sides,
        // clamped to the target.
        int left = std::clamp(static_cast<int>(std::floor(damage.left)), 0,
                              static_cast<int>(width));
        int top = std::clamp(static_cast<int>(std::floor(damage.top)), 0,
                             static_cast<int>(height));
        int right = std::clamp(static_cast<int>(std::ceil(damage.right)), 0,
                               static_cast<int>(width));
        int bottom = std::clamp(static_cast<int>(std::ceil(damage.bottom)), 0,
                                static_cast<int>(height));
        if (right > left && bottom > top) {
          blit_x0 = left;
          blit_x1 = right;
          blit_y0 = static_cast<int>(height) - bottom;
          blit_y1 = static_cast<int>(height) - top;
        }
      } else {
        preserved_surfaces_.insert(surface);
      }
    }
  }

  // Disable the scissor test as it can affect blit operations.
  // Prevents regressions like: https://github.com/flutter/flutter/issues/140828
  // See OpenGL specification version 4.6, section 18.3.1.
  gl_->Disable(GL_SCISSOR_TEST);
  gl_->BindFramebuffer(GL_READ_FRAMEBUFFER, source_id);
  gl_->BindFramebuffer(GL_DRAW_FRAMEBUFFER, kWindowFrameBufferId);

  auto blitFramebuffer = GetBlitFramebufferProc(*gl_);
  blitFramebuffer(blit_x0,              // srcX0
                  blit_y0,              // srcY0
                  blit_x1,              // srcX1
                  blit_y1,              // srcY1
                  blit_x0,              // dstX0
                  blit_y0,              // dstY0
                  blit_x1,              // dstX1
                  blit_y1,              // dstY1
                  GL_COLOR_BUFFER_BIT,  // mask
                  GL_NEAREST            // filter
  );

  int64_t swap_start = PresentLogNow();
  if (!surface->SwapBuffers()) {
    return false;
  }
  if (PresentLogEnabled()) {
    PresentLogWrite("frame", swap_start);
  }

  view->OnFramePresented();
  return true;
}

bool CompositorOpenGL::Initialize() {
  FML_DCHECK(!is_initialized_);

  egl::Manager* manager = engine_->egl_manager();
  if (!manager) {
    return false;
  }

  if (!manager->render_context()->MakeCurrent()) {
    return false;
  }

  gl_ = std::make_unique<impeller::ProcTableGLES>(resolver_);
  if (!gl_->IsValid()) {
    gl_.reset();
    return false;
  }

  if (gl_->GetDescription()->HasExtension("GL_EXT_texture_format_BGRA8888")) {
    format_.sized_format = GL_BGRA8_EXT;
    format_.general_format = GL_BGRA_EXT;
  } else {
    format_.sized_format = GL_RGBA8;
    format_.general_format = GL_RGBA;
  }

  if (!gl_->BlitFramebuffer.IsAvailable() &&
      !gl_->BlitFramebufferANGLE.IsAvailable()) {
    FML_LOG(ERROR) << "Unable to find OpenGL blit framebuffer procedure.";
    return false;
  }

  is_initialized_ = true;
  return true;
}

bool CompositorOpenGL::Clear(FlutterWindowsView* view) {
  FML_DCHECK(is_initialized_);

  // Check if this frame can be presented. This resizes the surface if needed.
  if (!view->OnEmptyFrameGenerated()) {
    return false;
  }

  // |OnEmptyFrameGenerated| should return false if the surface isn't valid.
  FML_DCHECK(view->surface() != nullptr);
  FML_DCHECK(view->surface()->IsValid());

  egl::WindowSurface* surface = view->surface();
  if (!surface->MakeCurrent()) {
    return false;
  }

  gl_->ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  gl_->Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  int64_t swap_start = PresentLogNow();
  if (!surface->SwapBuffers()) {
    return false;
  }
  if (PresentLogEnabled()) {
    PresentLogWrite("clear", swap_start);
  }

  view->OnFramePresented();
  return true;
}

}  // namespace flutter
