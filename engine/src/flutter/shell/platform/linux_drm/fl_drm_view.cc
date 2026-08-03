// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_view.h"

// Included above the other fl_drm headers: the VT-switch globals right below
// need the full FlDrmSeat definition.
#include "fl_drm_seat.h"

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <errno.h>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <libudev.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>

#include "embedder.h"

// Screenshot flags — set by SIGUSR1 or fl_drm_view_request_screenshot().
// The primary flag is consumed by RunCaptureHooks; the secondary flag by
// present_view for non-primary outputs (each output's next present writes
// /tmp/drm_screenshot_<connector>.ppm).
static volatile sig_atomic_t g_screenshot_requested = 0;
static volatile sig_atomic_t g_secondary_screenshot = 0;
static void ScreenshotSignalHandler(int) {
  g_screenshot_requested = 1;
  g_secondary_screenshot = 1;
}
void fl_drm_view_request_screenshot(void) {
  g_screenshot_requested = 1;
  g_secondary_screenshot = 1;
}

// ─── X11 screen capture ─────────────────────────────────────────────────────
// The in-tree X11 server implements GetImage (screen capture, e.g. Zoom screen
// share) by reading the presented desktop out of a CPU mirror that the present
// thread fills here. Capture only runs while a client is actively polling:
// fl_drm_view_arm_capture() sets a small frame countdown and schedules a frame,
// so the capture rate follows the client's GetImage rate and an idle desktop
// still refreshes the mirror once. The buffer holds glReadPixels output — RGBA,
// bottom-up; fl_drm_view_read_capture() flips and converts to X ZPixmap BGRX.
static std::mutex g_x11_cap_mu;
static std::vector<uint8_t> g_x11_cap_buf;  // RGBA, bottom-up
static uint32_t g_x11_cap_w = 0, g_x11_cap_h = 0;
static bool g_x11_cap_valid = false;
static std::atomic<int> g_x11_cap_frames{0};

// Video recording toggle — SIGRTMIN+1 starts/stops (SIGUSR2/SIGRTMIN are
// taken by VT switching below). While recording, every presented frame is
// downsampled 4x (nearest) and appended to /tmp/drm_record.bin as
// [uint64 CLOCK_MONOTONIC µs][RGB24, top-down]. Presents only happen when
// something changes, so the timestamps let the converter
// (tools/shell-drive.py in starling-os) rebuild a constant-rate video with
// held frames.
static volatile sig_atomic_t g_record_toggle = 0;
static void RecordSignalHandler(int) { g_record_toggle = 1; }

// Screen-recording API (fl_drm_view_recording_*) — same capture machinery as
// the signal toggle above, but full-resolution and delivered to a registered
// callback instead of a file. Requests are posted from any thread and
// consumed on the presenting thread in RunCaptureHooks; g_api_recording is
// the raster-thread truth the shell's frame pump polls.
static std::atomic<int> g_api_record_start{-1};  // pending downscale shift
static std::atomic<bool> g_api_record_stop{false};
static std::atomic<bool> g_api_recording{false};
// Capture crop, top-down framebuffer px — {x, y, w, h}. w<=0 means the whole
// output (screen recording). Written from any thread (start, and per-frame
// set_crop while a window recording tracks its window), read per present.
// Torn reads across the four values skew one frame by a few px — harmless.
static std::atomic<int> g_api_crop[4] = {0, 0, 0, 0};
// True app capture: an external texture id (>= 0) to record INSTEAD of the
// framebuffer — the window's own composited content, resolved through the
// same callback the compositor uses, so overlap and position are invisible
// to the recording. -1 = capture the output/crop.
static std::atomic<int64_t> g_api_record_texture{-1};
// Whether the recorded texture's content is top-down (Wayland client
// buffers) or bottom-up (first-party children render into GL FBOs) — the
// blit flips the latter. The scene has the same split: flipTextureY.
static std::atomic<bool> g_api_record_tex_topdown{false};

// Zero-copy sink (see fl_drm_view.h): armed per session by the shell,
// consumed with the start request. The busy mask is the only recording state
// touched off the presenting thread — release_dmabuf_slot clears bits from
// the encoder's thread while presents test-and-set them.
static std::atomic<bool> g_api_record_dmabuf{false};
constexpr int kDmabufRing = 4;
static std::atomic<uint32_t> g_dmabuf_busy{0};
// App capture: bumped by the shell whenever the RECORDED window commits new
// content. A window's pixels cannot change without one, so a present that
// carries no new commit would encode a byte-identical frame — the capture
// skips those (bounded by kRecKeepaliveUs so the timeline can't drift).
static std::atomic<uint64_t> g_api_record_epoch{0};
constexpr uint64_t kRecKeepaliveUs = 300000;  // 0.3s

// VT switching — signal handlers run on any thread, set flags for event loop.
// SIGUSR2 = VT release (kernel wants us to give up the VT).
// SIGRTMIN = VT acquire (kernel is giving us the VT back).
static volatile sig_atomic_t g_vt_pending_release = 0;
static volatile sig_atomic_t g_vt_pending_acquire = 0;
static int g_vt_tty_fd = -1;
static int g_vt_drm_fd = -1;
static volatile bool* g_vt_active_ptr = nullptr;

static void VtReleaseHandler(int) {
  // Drop DRM master immediately so the other VT can use the GPU.
  if (g_vt_drm_fd >= 0) drmDropMaster(g_vt_drm_fd);
  // Mark inactive for the swap chain (raster thread).
  if (g_vt_active_ptr) *g_vt_active_ptr = false;
  // Acknowledge the VT release to the kernel.
  if (g_vt_tty_fd >= 0) ioctl(g_vt_tty_fd, VT_RELDISP, 1);
  g_vt_pending_release = 1;
}

static void VtAcquireHandler(int) {
  // Reclaim DRM master.
  if (g_vt_drm_fd >= 0) drmSetMaster(g_vt_drm_fd);
  // Don't set vt_active here — event loop sets it after restoring modeset.
  // Acknowledge the VT acquire to the kernel.
  if (g_vt_tty_fd >= 0) ioctl(g_vt_tty_fd, VT_RELDISP, VT_ACKACQ);
  g_vt_pending_acquire = 1;
}

// Seat for the running view (libseat mode) — the VT-switch request and the
// platform thread need it outside the view pointer.
static flutter::FlDrmSeat* g_seat_ptr = nullptr;

// Called from the input layer on Ctrl+Alt+Fn: with the VT keyboard in K_OFF
// the kernel no longer switches consoles itself, so the shell asks for it.
// Direct mode: VT_ACTIVATE + the VT_PROCESS release handshake (SIGUSR2 ->
// VT_RELDISP). libseat mode: the seat manager owns VTs — ask it instead.
extern "C" void fl_drm_request_vt_switch(int vt) {
  if (g_seat_ptr && g_seat_ptr->via_libseat()) {
    if (vt > 0) {
      g_seat_ptr->SwitchSession(vt);
    }
    return;
  }
  if (g_vt_tty_fd >= 0 && vt > 0) {
    ioctl(g_vt_tty_fd, VT_ACTIVATE, vt);
  }
}


// GCD main queue integration — drain DispatchQueue.main from the epoll loop
// so that @MainActor / async-await continuations fire on the main thread.
// Resolved via dlsym at runtime (libdispatch is loaded by the Swift process).
#include <dlfcn.h>

// Function pointers resolved at runtime from libdispatch.
static int (*gcd_get_main_queue_handle)(void) = nullptr;
static void (*gcd_main_queue_drain)(void*) = nullptr;
static void* (*gcd_get_main_queue)(void) = nullptr;
static void (*gcd_dispatch_async_f)(void*, void*, void(*)(void*)) = nullptr;

static void InitGCDIntegration() {
  gcd_get_main_queue_handle = reinterpret_cast<int(*)(void)>(
      dlsym(RTLD_DEFAULT, "_dispatch_get_main_queue_handle_4CF"));
  gcd_main_queue_drain = reinterpret_cast<void(*)(void*)>(
      dlsym(RTLD_DEFAULT, "_dispatch_main_queue_callback_4CF"));
  gcd_get_main_queue = reinterpret_cast<void*(*)(void)>(
      dlsym(RTLD_DEFAULT, "dispatch_get_main_queue"));
  gcd_dispatch_async_f = reinterpret_cast<void(*)(void*, void*, void(*)(void*))>(
      dlsym(RTLD_DEFAULT, "dispatch_async_f"));
  if (gcd_get_main_queue_handle && gcd_main_queue_drain) {
    fprintf(stderr, "[DrmView] GCD main queue integration enabled\n");
  } else {
    fprintf(stderr, "[DrmView] GCD main queue integration unavailable\n");
  }
}

#include "fl_drm_cursor.h"
#include "fl_drm_display.h"
#include "fl_drm_egl.h"
#include "fl_drm_gbm.h"
#include "fl_drm_input.h"
#include "fl_drm_swap_chain.h"
#include "fl_drm_task_runner.h"

using namespace flutter;

// The view struct — ties all components together.
struct FlDrmView {
  // Declared first: destroyed last — the display closes its fd through it.
  FlDrmSeat seat;
  FlDrmDisplay display;
  FlDrmGbm gbm;  // declared before egl: EGL surfaces die before GBM surfaces
  FlDrmEgl egl;
  // Per-output scanout resources, parallel to display.output(i). The engine's
  // implicit view renders to the primary chain; secondary outputs are modeset
  // black until per-output Flutter views land (multi-view compositor API).
  struct OutputResources {
    gbm_surface* gbm_surface_ptr = nullptr;   // owned by FlDrmGbm
    EGLSurface egl_surface = EGL_NO_SURFACE;  // owned by FlDrmEgl
    FlDrmSwapChain* chain = nullptr;          // owned here
  };
  std::vector<OutputResources> output_resources;
  FlDrmSwapChain* swap_chain = nullptr;  // primary output's chain (engine path)
  std::vector<std::thread> secondary_test_threads;

  // FlutterCompositor path: the engine renders each view into an FBO backing
  // store; present_view blits it onto the mapped output's window surface and
  // page-flips that output's chain. Default on (FLUTTER_DRM_COMPOSITOR=0
  // falls back to the direct single-view present path); required for
  // FlutterEngineAddView views.
  bool use_compositor = false;
  bool multi_view = false;  // secondary outputs were given Flutter views
  FlutterCompositor compositor = {};
  // Virtual-desktop placement per output (parallel to display.output(i)),
  // driving the input layer's pointer regions. Primary placed at create.
  struct OutputPlacement {
    bool placed = false;
    double logical_x = 0;
    double logical_y = 0;
    double scale = 1.0;
  };
  std::vector<OutputPlacement> placements;

  // Hotplug: udev monitor on the DRM subsystem (platform thread) + the
  // app's outputs-changed callback. modeset_context is a dedicated EGL
  // context for hotplug-time InitialModeSet — the render context is
  // current on the raster thread by then.
  struct udev* hotplug_udev = nullptr;
  struct udev_monitor* hotplug_monitor = nullptr;
  EGLContext modeset_context = EGL_NO_CONTEXT;
  FlDrmOutputsChangedCallback outputs_changed_callback = nullptr;
  void* outputs_changed_user_data = nullptr;

  // Outputs whose Flutter view the engine has finished removing (the
  // RemoveView callback fires on an engine thread; the actual resource
  // teardown must happen on the platform thread). {output index, view id}.
  std::mutex teardown_mutex;
  std::vector<std::pair<size_t, int64_t>> pending_teardowns;
  // Flutter view id → output index. Guarded: present_view runs on the raster
  // thread while views are added on the main thread.
  std::mutex view_map_mutex;
  std::map<int64_t, size_t> view_to_output;
  int64_t next_view_id = 1;  // view 0 is the implicit view (primary)

  // Runs before member destructors: chains go first, then EGL surfaces,
  // then GBM surfaces, then the display fd.
  ~FlDrmView() {
    for (auto& res : output_resources) {
      delete res.chain;
      res.chain = nullptr;
    }
    swap_chain = nullptr;
  }
  FlDrmTaskRunner task_runner;
  FlDrmInput input;
  FlDrmCursor cursor;

  FlutterEngine engine = nullptr;
  volatile bool running = false;
  int epoll_fd = -1;

  // VT switching.
  int vt_tty_fd = -1;
  int vt_num = 0;
  volatile bool vt_active = true;
  bool vt_initialized = false;
  int vt_saved_kb_mode = -1;  // console keyboard mode to restore on exit

  // External texture callback (set from Swift via fl_drm_view_set_external_texture_callback)
  FlDrmExternalTextureCallback external_texture_callback = nullptr;
  void* external_texture_user_data = nullptr;

  // Present (page-flip) callback — set from Swift, invoked on the platform
  // thread by the swap chain when a flip lands.
  FlDrmPresentCallback present_callback = nullptr;
  void* present_callback_user_data = nullptr;

  // Screen-recording frame callback — set from Swift, invoked on the
  // recorder's writer thread (see fl_drm_view.h).
  FlDrmRecordFrameCallback record_frame_callback = nullptr;
  void* record_frame_user_data = nullptr;
  // Zero-copy sibling: dmabuf frames, invoked on the presenting thread.
  FlDrmRecordDmabufCallback record_dmabuf_callback = nullptr;
  void* record_dmabuf_user_data = nullptr;
  uint32_t refresh_ns = 0;  // display refresh period, from the DRM mode

  // Damage tracking for partial repaint.
  // Stores the last 2 frames' frame_damage rects so we can report
  // existing_damage to the engine (covers double and triple buffering).
  FlutterRect prev_damage[2] = {};
  int damage_history_count = 0;
  // Scratch rect returned by populate_existing_damage callback.
  FlutterRect existing_damage_scratch = {};

  // External epoll fd — allows Swift to register extra fds (e.g. Wayland server).
  static constexpr int kMaxExternalFds = 16;
  struct ExternalFd {
    int fd = -1;
    void (*callback)(void* user_data) = nullptr;
    void* user_data = nullptr;
  };
  ExternalFd external_fds[kMaxExternalFds] = {};
  int external_fd_count = 0;

  // UI task runner (GCD main queue) — separate from platform task runner.
  // Tasks posted here execute on the main thread via DispatchQueue.main.
  struct UITask {
    FlutterTask task;
    uint64_t target_time_nanos;
  };
  std::mutex ui_mutex;
  std::vector<UITask> ui_tasks;
  int ui_wakeup_read_fd = -1;   // Pipe to wake main thread on UI task post
  int ui_wakeup_write_fd = -1;
  pthread_t main_thread = 0;
  pthread_t platform_thread = 0;
};

// ─── ES3 entry points ────────────────────────────────────────────────────────
// Headers here are GLES2-only; the context Mesa hands back is ES 3.x, so the
// ES3 functions and enums used by the compositor blit, video recording, and
// backing stores are resolved/defined at runtime.
constexpr GLenum GL_READ_FRAMEBUFFER_ = 0x8CA8;
constexpr GLenum GL_DRAW_FRAMEBUFFER_ = 0x8CA9;
constexpr GLenum GL_RGBA8_ = 0x8058;
constexpr GLenum GL_PIXEL_PACK_BUFFER_ = 0x88EB;
constexpr GLenum GL_STREAM_READ_ = 0x88E1;
constexpr GLbitfield GL_MAP_READ_BIT_ = 0x0001;
constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT_ = 0x821A;
constexpr GLenum GL_DEPTH24_STENCIL8_ = 0x88F0;

struct Es3Fns {
  typedef void (*BlitFn)(GLint, GLint, GLint, GLint, GLint, GLint, GLint,
                         GLint, GLbitfield, GLenum);
  typedef void* (*MapRangeFn)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
  typedef GLboolean (*UnmapFn)(GLenum);
  BlitFn blit = nullptr;
  MapRangeFn map = nullptr;
  UnmapFn unmap = nullptr;
};

// Requires a current GL context on first call (glGetString).
static const Es3Fns& GetEs3Fns() {
  static Es3Fns fns;
  static bool checked = false;
  if (!checked) {
    checked = true;
    const char* ver = (const char*)glGetString(GL_VERSION);
    if (ver && strstr(ver, "OpenGL ES 3")) {
      fns.blit = (Es3Fns::BlitFn)eglGetProcAddress("glBlitFramebuffer");
      fns.map = (Es3Fns::MapRangeFn)eglGetProcAddress("glMapBufferRange");
      fns.unmap = (Es3Fns::UnmapFn)eglGetProcAddress("glUnmapBuffer");
    }
  }
  return fns;
}

// Paint the cursor into a captured frame (top-down RGBA, downscaled by
// |shift|). The cursor scans out on the DRM cursor plane, so no GL readback
// contains it — a recording without a pointer looks broken. Snapshot coords
// are full-res CRTC-local with the hot-spot already subtracted. Writer-thread
// only (the shape cache below is unsynchronized on purpose: there is at most
// one recording session, hence one writer, at a time).
static void BlendCursorOverlay(uint8_t* frame,
                               uint32_t fw,
                               uint32_t fh,
                               int shift,
                               const flutter::FlCursorSnapshot& cur) {
  static uint8_t bitmap[64 * 64 * 4];
  static int bitmap_shape = -1;
  if (bitmap_shape != static_cast<int>(cur.shape)) {
    flutter::FlDrmCursor::RenderShapeRGBA(cur.shape, bitmap);
    bitmap_shape = static_cast<int>(cur.shape);
  }
  const uint32_t size = 64u >> shift;
  for (uint32_t ty = 0; ty < size; ty++) {
    const int fy = (cur.y >> shift) + static_cast<int>(ty);
    if (fy < 0 || fy >= static_cast<int>(fh)) continue;
    for (uint32_t tx = 0; tx < size; tx++) {
      const int fx = (cur.x >> shift) + static_cast<int>(tx);
      if (fx < 0 || fx >= static_cast<int>(fw)) continue;
      // Nearest sample; the bitmaps are hard-edged (opaque or clear), so
      // alpha is a mask, not a blend factor.
      const uint8_t* s = bitmap + (((ty << shift) * 64 + (tx << shift)) * 4);
      if (s[3] == 0) continue;
      uint8_t* d = frame + (static_cast<size_t>(fy) * fw + fx) * 4;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }
  }
}

// ─── Zero-copy recording ring ────────────────────────────────────────────────
// GBM-allocated linear buffers the capture blit lands in directly; their
// dmabuf fds go to the shell's hardware encoder and the pixels never touch
// the CPU. All GL/EGL work happens on the presenting thread with its context
// current — only the busy mask (above) is shared with the encoder's thread.

constexpr EGLenum EGL_LINUX_DMA_BUF_EXT_ = 0x3270;
constexpr EGLint EGL_LINUX_DRM_FOURCC_EXT_ = 0x3271;
constexpr EGLint EGL_DMA_BUF_PLANE0_FD_EXT_ = 0x3272;
constexpr EGLint EGL_DMA_BUF_PLANE0_OFFSET_EXT_ = 0x3273;
constexpr EGLint EGL_DMA_BUF_PLANE0_PITCH_EXT_ = 0x3274;
constexpr uint32_t DRM_FORMAT_ABGR8888_ = 0x34324241;  // 'AB24': R,G,B,A bytes
constexpr uint64_t DRM_FORMAT_MOD_INVALID_ = 0x00ffffffffffffffull;
constexpr GLenum GL_VERTEX_ARRAY_BINDING_ = 0x85B5;

struct ZeroCopyFns {
  typedef void* (*CreateImageFn)(EGLDisplay, EGLContext, EGLenum, void*,
                                 const EGLint*);
  typedef EGLBoolean (*DestroyImageFn)(EGLDisplay, void*);
  typedef void (*ImageTargetRboFn)(GLenum, void*);
  typedef void (*BindVertexArrayFn)(GLuint);
  CreateImageFn create_image = nullptr;
  DestroyImageFn destroy_image = nullptr;
  ImageTargetRboFn image_target_rbo = nullptr;
  BindVertexArrayFn bind_vertex_array = nullptr;
  bool complete() const {
    return create_image && destroy_image && image_target_rbo &&
           bind_vertex_array;
  }
};

static const ZeroCopyFns& GetZeroCopyFns() {
  static ZeroCopyFns fns;
  static bool checked = false;
  if (!checked) {
    checked = true;
    fns.create_image =
        (ZeroCopyFns::CreateImageFn)eglGetProcAddress("eglCreateImageKHR");
    fns.destroy_image =
        (ZeroCopyFns::DestroyImageFn)eglGetProcAddress("eglDestroyImageKHR");
    fns.image_target_rbo = (ZeroCopyFns::ImageTargetRboFn)eglGetProcAddress(
        "glEGLImageTargetRenderbufferStorageOES");
    fns.bind_vertex_array =
        (ZeroCopyFns::BindVertexArrayFn)eglGetProcAddress("glBindVertexArray");
  }
  return fns;
}

struct DmabufSlot {
  gbm_bo* bo = nullptr;
  int fd = -1;         // exported once; owned here, engine lifetime
  void* image = nullptr;  // EGLImageKHR
  GLuint rbo = 0;
  GLuint fbo = 0;
  uint32_t stride = 0;
  uint64_t modifier = 0;
};
struct DmabufRing {
  uint32_t w = 0, h = 0;
  DmabufSlot slots[kDmabufRing];
};
static DmabufRing g_dmabuf_ring;

static void FreeDmabufRing(FlDrmView* v) {
  const ZeroCopyFns& fns = GetZeroCopyFns();
  for (DmabufSlot& s : g_dmabuf_ring.slots) {
    if (s.fbo) glDeleteFramebuffers(1, &s.fbo);
    if (s.rbo) glDeleteRenderbuffers(1, &s.rbo);
    if (s.image && fns.destroy_image) fns.destroy_image(v->egl.display(), s.image);
    if (s.fd >= 0) close(s.fd);
    if (s.bo) gbm_bo_destroy(s.bo);
    s = DmabufSlot();
  }
  g_dmabuf_ring.w = g_dmabuf_ring.h = 0;
}

// The ring outlives the session that built it (slots may still be held by
// the encoder when the stop is consumed) and is recycled when the next
// session matches its size. Returns false — caller falls back to CPU
// frames — when the GBM/EGL path is unavailable, any slot fails to build,
// or a stale ring of the wrong size is still partly held.
static bool EnsureDmabufRing(FlDrmView* v, uint32_t w, uint32_t h) {
  const ZeroCopyFns& fns = GetZeroCopyFns();
  if (!fns.complete() || !v->gbm.device()) return false;
  if (g_dmabuf_ring.w == w && g_dmabuf_ring.h == h) return true;
  if (g_dmabuf_busy.load(std::memory_order_acquire) != 0) return false;
  FreeDmabufRing(v);
  for (DmabufSlot& s : g_dmabuf_ring.slots) {
    // Linear so VAAPI import needs no modifier support; the blit into a
    // linear target costs the GPU a little and the CPU nothing.
    s.bo = gbm_bo_create(v->gbm.device(), w, h, GBM_FORMAT_ABGR8888,
                         GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!s.bo) { FreeDmabufRing(v); return false; }
    s.fd = gbm_bo_get_fd(s.bo);
    s.stride = gbm_bo_get_stride(s.bo);
    uint64_t mod = gbm_bo_get_modifier(s.bo);
    s.modifier = (mod == DRM_FORMAT_MOD_INVALID_) ? 0 : mod;
    if (s.fd < 0) { FreeDmabufRing(v); return false; }
    const EGLint attrs[] = {
        EGL_WIDTH, (EGLint)w, EGL_HEIGHT, (EGLint)h,
        EGL_LINUX_DRM_FOURCC_EXT_, (EGLint)DRM_FORMAT_ABGR8888_,
        EGL_DMA_BUF_PLANE0_FD_EXT_, s.fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT_, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT_, (EGLint)s.stride,
        EGL_NONE};
    s.image = fns.create_image(v->egl.display(), EGL_NO_CONTEXT,
                               EGL_LINUX_DMA_BUF_EXT_, nullptr, attrs);
    if (!s.image) { FreeDmabufRing(v); return false; }
    glGenRenderbuffers(1, &s.rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, s.rbo);
    fns.image_target_rbo(GL_RENDERBUFFER, s.image);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glGenFramebuffers(1, &s.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, s.rbo);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) { FreeDmabufRing(v); return false; }
  }
  g_dmabuf_ring.w = w;
  g_dmabuf_ring.h = h;
  return true;
}

// Draw the cursor into the bound draw framebuffer (the dmabuf slot) — the
// GPU twin of BlendCursorOverlay above, for frames the CPU never sees.
// Alpha-blends a 64x64 shape texture at the snapshot position. Presenting
// thread; saves and restores every piece of GL state it touches, since it
// runs inside the engine's present with Skia's state live.
static void DrawCursorGpu(const flutter::FlCursorSnapshot& cur,
                          uint32_t rw, uint32_t rh, int shift) {
  static GLuint prog = 0, tex = 0;
  static GLint u_rect = -1;
  static int tex_shape = -1;
  static bool failed = false;
  if (failed) return;
  const ZeroCopyFns& fns = GetZeroCopyFns();
  if (prog == 0) {
    const char* vs_src =
        "#version 300 es\n"
        "uniform vec4 u_rect;\n"
        "out vec2 v_uv;\n"
        "void main() {\n"
        "  vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
        "  v_uv = c;\n"
        "  gl_Position = vec4(mix(u_rect.xy, u_rect.zw, c), 0.0, 1.0);\n"
        "}\n";
    const char* fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D u_tex;\n"
        "in vec2 v_uv;\n"
        "out vec4 frag;\n"
        "void main() { frag = texture(u_tex, v_uv); }\n";
    auto compile = [](GLenum type, const char* src) -> GLuint {
      GLuint sh = glCreateShader(type);
      glShaderSource(sh, 1, &src, nullptr);
      glCompileShader(sh);
      GLint ok = 0;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) { glDeleteShader(sh); return 0; }
      return sh;
    };
    GLuint vs = compile(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
      if (vs) glDeleteShader(vs);
      if (fs) glDeleteShader(fs);
      failed = true;
      fprintf(stderr, "[DrmView] cursor shader failed — recording cursor-less\n");
      return;
    }
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      glDeleteProgram(prog);
      prog = 0;
      failed = true;
      fprintf(stderr, "[DrmView] cursor shader failed — recording cursor-less\n");
      return;
    }
    u_rect = glGetUniformLocation(prog, "u_rect");
    glGenTextures(1, &tex);
  }

  GLint prev_prog = 0, prev_active = 0, prev_tex = 0, prev_vao = 0;
  GLint prev_viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev_prog);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING_, &prev_vao);
  glGetIntegerv(GL_VIEWPORT, prev_viewport);
  GLboolean was_blend = glIsEnabled(GL_BLEND);
  GLboolean was_scissor = glIsEnabled(GL_SCISSOR_TEST);
  GLboolean was_depth = glIsEnabled(GL_DEPTH_TEST);
  GLboolean was_stencil = glIsEnabled(GL_STENCIL_TEST);
  GLboolean was_cull = glIsEnabled(GL_CULL_FACE);
  GLint bsrgb = 0, bdrgb = 0, bsa = 0, bda = 0;
  glGetIntegerv(GL_BLEND_SRC_RGB, &bsrgb);
  glGetIntegerv(GL_BLEND_DST_RGB, &bdrgb);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsa);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &bda);

  glBindTexture(GL_TEXTURE_2D, tex);
  if (tex_shape != static_cast<int>(cur.shape)) {
    static uint8_t bitmap[64 * 64 * 4];
    flutter::FlDrmCursor::RenderShapeRGBA(cur.shape, bitmap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    tex_shape = static_cast<int>(cur.shape);
  }

  glUseProgram(prog);
  fns.bind_vertex_array(0);
  glViewport(0, 0, (GLsizei)rw, (GLsizei)rh);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  // The slot holds a top-down image (the capture blit flips), so image y
  // grows with NDC y: row 0 = NDC -1.
  const float size = (float)(64u >> shift);
  const float x0 = (float)(cur.x >> shift), y0 = (float)(cur.y >> shift);
  glUniform4f(u_rect, x0 * 2.f / rw - 1.f, y0 * 2.f / rh - 1.f,
              (x0 + size) * 2.f / rw - 1.f, (y0 + size) * 2.f / rh - 1.f);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  if (!was_blend) glDisable(GL_BLEND);
  glBlendFuncSeparate(bsrgb, bdrgb, bsa, bda);
  if (was_scissor) glEnable(GL_SCISSOR_TEST);
  if (was_depth) glEnable(GL_DEPTH_TEST);
  if (was_stencil) glEnable(GL_STENCIL_TEST);
  if (was_cull) glEnable(GL_CULL_FACE);
  glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2],
             prev_viewport[3]);
  fns.bind_vertex_array((GLuint)prev_vao);
  glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex);
  glActiveTexture((GLenum)prev_active);
  glUseProgram((GLuint)prev_prog);
}

// ─── Capture hooks (screenshot / video recording / debug stats) ─────────────
// Observes the final presented pixels: call on the presenting thread with the
// frame bound as the GL READ framebuffer, before the swap. Primary output
// only. Invoked by both present paths (legacy present_with_info and the
// compositor's present_view).
static void RunCaptureHooks(FlDrmView* v, uint32_t width, uint32_t height) {
  // [STARLING-DEBUG] auto-capture the first frame to diagnose black-frame vs
  // flip-not-latching. Prints render stats to stderr (no file transfer).
  static int s_capture_frame_n = 0;
  const char* dbg_env = getenv("STARLING_DEBUG_CAPTURE");
  if (dbg_env && dbg_env[0] && s_capture_frame_n++ == 0) {
    uint32_t w = width, h = height;
    uint8_t* px = new uint8_t[w * h * 4];
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    size_t nb = 0, n = (size_t)w * h;
    for (size_t i = 0; i < n; i++) {
      if (px[i * 4] > 24 || px[i * 4 + 1] > 24 || px[i * 4 + 2] > 24) nb++;
    }
    size_t c = (n / 2) * 4;
    fprintf(stderr,
            "[STARLING] rendered frame0: %ux%u nonblack=%.3f center_rgba=%u,%u,%u,%u\n",
            w, h, (double)nb / (double)n, px[c], px[c + 1], px[c + 2],
            px[c + 3]);
    delete[] px;
  }

  // Screenshot capture (before swap).
  if (g_screenshot_requested) {
    g_screenshot_requested = 0;
    uint32_t w = width, h = height;
    uint8_t* pixels = new uint8_t[w * h * 4];
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    static int shot_count = 0;
    char path[128];
    snprintf(path, sizeof(path), "/tmp/drm_screenshot_%d.ppm", shot_count++);
    FILE* f = fopen(path, "wb");
    if (f) {
      fprintf(f, "P6\n%u %u\n255\n", w, h);
      for (int y = h - 1; y >= 0; y--) {
        for (uint32_t x = 0; x < w; x++) {
          uint8_t* p = pixels + (y * w + x) * 4;
          fwrite(p, 1, 3, f);
        }
      }
      fclose(f);
      fprintf(stderr, "[DrmView] Screenshot saved to %s\n", path);
    }
    delete[] pixels;
  }

  // X11 GetImage capture: mirror the presented frame for the X server to read
  // on its own thread. Armed per GetImage; the countdown bounds the readback
  // cost when the client polls slower than the desktop presents.
  if (g_x11_cap_frames.load(std::memory_order_relaxed) > 0) {
    g_x11_cap_frames.fetch_sub(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(g_x11_cap_mu);
    size_t need = static_cast<size_t>(width) * height * 4;
    if (g_x11_cap_buf.size() < need) {
      g_x11_cap_buf.resize(need);
    }
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_x11_cap_buf.data());
    g_x11_cap_w = width;
    g_x11_cap_h = height;
    g_x11_cap_valid = true;
  }

  // Video recording — one capture machinery, two triggers/sinks:
  //   SIGRTMIN+1 (debug): 4x-downsampled RGB24 appended to
  //   /tmp/drm_record.bin, converted offline by build/shell-drive.py.
  //   fl_drm_view_recording_* (the shell's screen recorder): frames handed
  //   to the registered callback, cursor composited in.
  // Capture must not perturb the desktop's present path, so it is fully
  // asynchronous:
  //   present thread: GPU blit into a capture FBO, then glReadPixels into a
  //   PBO ring — both non-blocking. kRecRing presents later the PBO is
  //   mapped (its DMA long finished) and the pixels are memcpy'd to a
  //   queue; a background writer thread does the packing/compositing and
  //   the file IO or callback.
  // Needs an ES3 context (Mesa hands back 3.x even though we ask for 2).
  struct RecFrame {
    uint64_t ts;
    flutter::FlCursorSnapshot cursor;
    std::vector<uint8_t> rgba;
  };
  struct RecWriter {
    std::thread th;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<RecFrame> q;
    bool done = false;
    // Exactly one sink is set: the debug file or the shell's callback.
    FILE* file = nullptr;
    FlDrmRecordFrameCallback cb = nullptr;
    void* cb_user = nullptr;
    uint32_t rw = 0, rh = 0;
    int shift = 0;  // frame = full-res >> shift
    int dropped = 0;
    // Zero-copy session: frames go to the dmabuf ring and the dmabuf
    // callback straight from the presenting thread — no PBOs, no writer
    // thread (th never starts).
    bool dmabuf = false;
    // App-capture redundancy skip: the commit epoch and wall clock of the
    // last delivered frame, and how many presents were skipped as
    // unchanged (logged at stop alongside drops).
    uint64_t last_epoch = 0;
    uint64_t last_frame_us = 0;
    int skipped = 0;
  };
  static RecWriter* rec = nullptr;
  constexpr int kRecRing = 3;
  static GLuint rec_pbo[kRecRing] = {0, 0, 0};
  static uint64_t rec_pbo_ts[kRecRing] = {0, 0, 0};
  static flutter::FlCursorSnapshot rec_pbo_cursor[kRecRing];
  static bool rec_pbo_pending[kRecRing] = {false, false, false};
  static int rec_slot = 0;
  static size_t rec_pbo_bytes = 0;

  const Es3Fns& es3 = GetEs3Fns();

  // Collect a completed PBO slot: map (non-blocking by now) and hand the
  // pixels to the writer thread. Bounded queue — drop rather than stall.
  auto drain_slot = [&](int slot) {
    if (!rec || !rec_pbo_pending[slot]) return;
    rec_pbo_pending[slot] = false;
    size_t nbytes = (size_t)rec->rw * rec->rh * 4;
    glBindBuffer(GL_PIXEL_PACK_BUFFER_, rec_pbo[slot]);
    void* p = es3.map(GL_PIXEL_PACK_BUFFER_, 0, (GLsizeiptr)nbytes,
                      GL_MAP_READ_BIT_);
    if (p) {
      bool notify = false;
      {
        std::lock_guard<std::mutex> lk(rec->mu);
        if (rec->q.size() < 8) {
          RecFrame fr;
          fr.ts = rec_pbo_ts[slot];
          fr.cursor = rec_pbo_cursor[slot];
          fr.rgba.assign((uint8_t*)p, (uint8_t*)p + nbytes);
          rec->q.push_back(std::move(fr));
          notify = true;
        } else {
          rec->dropped++;
        }
      }
      if (notify) rec->cv.notify_one();
      es3.unmap(GL_PIXEL_PACK_BUFFER_);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
  };

  const bool have_es3 = es3.blit && es3.map && es3.unmap;

  // The writer drains the frame queue until done: packs RGB24 to the file
  // (debug sink) or composites the cursor and invokes the callback (API
  // sink), then exits when done and drained.
  auto start_writer = [](RecWriter* r) {
    r->th = std::thread([r]() {
      std::vector<uint8_t> rgb;
      if (r->file) {
        rgb.resize((size_t)r->rw * r->rh * 3);
      }
      for (;;) {
        RecFrame fr;
        {
          std::unique_lock<std::mutex> lk(r->mu);
          r->cv.wait(lk, [&] { return r->done || !r->q.empty(); });
          if (r->q.empty()) break;  // done and drained
          fr = std::move(r->q.front());
          r->q.pop_front();
        }
        if (r->file) {
          const uint8_t* src = fr.rgba.data();
          size_t n = (size_t)r->rw * r->rh;
          for (size_t i = 0; i < n; i++) {
            rgb[i * 3 + 0] = src[i * 4 + 0];
            rgb[i * 3 + 1] = src[i * 4 + 1];
            rgb[i * 3 + 2] = src[i * 4 + 2];
          }
          fwrite(&fr.ts, sizeof(fr.ts), 1, r->file);
          fwrite(rgb.data(), 1, rgb.size(), r->file);
        } else {
          if (fr.cursor.visible) {
            BlendCursorOverlay(fr.rgba.data(), r->rw, r->rh, r->shift,
                               fr.cursor);
          }
          r->cb(r->cb_user, fr.rgba.data(), r->rw, r->rh, fr.ts);
        }
      }
    });
  };

  // Stop: collect outstanding slots, let the writer finish, close. The join
  // stalls the present thread for the tail of the queue (≤11 frames) — a
  // one-time hiccup at stop, same trade the debug path has always made.
  auto stop_rec = [&]() {
    for (int i = 0; i < kRecRing; i++)
      drain_slot((rec_slot + i) % kRecRing);
    if (rec->th.joinable()) {  // dmabuf sessions never start the writer
      {
        std::lock_guard<std::mutex> lk(rec->mu);
        rec->done = true;
      }
      rec->cv.notify_one();
      rec->th.join();
    }
    if (rec->file) fclose(rec->file);
    if (rec->dropped)
      fprintf(stderr, "[DrmView] Recording dropped %d frames\n",
              rec->dropped);
    if (rec->skipped)
      fprintf(stderr, "[DrmView] Recording skipped %d unchanged frames\n",
              rec->skipped);
    fprintf(stderr, "[DrmView] Recording stopped\n");
    delete rec;
    rec = nullptr;
    for (int i = 0; i < kRecRing; i++) rec_pbo_pending[i] = false;
    g_api_recording.store(false, std::memory_order_release);
  };

  if (g_record_toggle) {
    g_record_toggle = 0;
    if (!rec) {
      if (have_es3) {
        FILE* f = fopen("/tmp/drm_record.bin", "wb");
        if (f) {
          rec = new RecWriter();
          rec->file = f;
          rec->rw = width / 4;
          rec->rh = height / 4;
          rec->shift = 2;
          start_writer(rec);
          fprintf(stderr,
                  "[DrmView] Recording started -> /tmp/drm_record.bin\n");
        }
      } else {
        fprintf(stderr,
                "[DrmView] Recording unavailable: needs an ES3 context\n");
      }
    } else if (rec->file) {
      stop_rec();
    } else {
      fprintf(stderr,
              "[DrmView] SIGRTMIN+1 ignored: recording API session active\n");
    }
  }

  // API stop before start, so a fast stop→start toggle ends up recording.
  if (g_api_record_stop.exchange(false, std::memory_order_acq_rel) && rec &&
      rec->cb) {
    stop_rec();
  }
  int api_shift = g_api_record_start.exchange(-1, std::memory_order_acq_rel);
  if (api_shift >= 0) {
    if (rec) {
      fprintf(stderr,
              "[DrmView] recording_start ignored: already recording\n");
    } else if (!v->record_frame_callback) {
      fprintf(stderr,
              "[DrmView] recording_start ignored: no frame callback\n");
    } else if (!have_es3) {
      fprintf(stderr,
              "[DrmView] Recording unavailable: needs an ES3 context\n");
    } else {
      // Output dims freeze at start (the encoder is fixed-size): the crop
      // may move and resize while recording — later frames scale into
      // these dims.
      uint32_t cw = (uint32_t)std::max(0, g_api_crop[2].load());
      uint32_t ch = (uint32_t)std::max(0, g_api_crop[3].load());
      if (cw == 0 || ch == 0) { cw = width; ch = height; }
      rec = new RecWriter();
      rec->cb = v->record_frame_callback;
      rec->cb_user = v->record_frame_user_data;
      rec->shift = api_shift;
      rec->rw = std::max(1u, cw >> api_shift);
      rec->rh = std::max(1u, ch >> api_shift);
      // Zero-copy when the shell armed it: build (or recycle) the dmabuf
      // ring now, with the GL context current. Failure sends the one-shot
      // sentinel so the shell swaps to its pipe encoder, and the session
      // continues as a plain CPU one.
      if (g_api_record_dmabuf.load(std::memory_order_acquire) &&
          v->record_dmabuf_callback) {
        if (EnsureDmabufRing(v, rec->rw, rec->rh)) {
          rec->dmabuf = true;
        } else {
          FlDrmRecordDmabufFrame sentinel = {};
          sentinel.slot = -1;
          sentinel.fd = -1;
          v->record_dmabuf_callback(v->record_dmabuf_user_data, &sentinel);
          fprintf(stderr,
                  "[DrmView] dmabuf ring unavailable — CPU frames instead\n");
        }
      }
      if (!rec->dmabuf) start_writer(rec);
      g_api_recording.store(true, std::memory_order_release);
      fprintf(stderr, "[DrmView] Recording started -> %s (%ux%u)\n",
              rec->dmabuf ? "dmabuf callback" : "callback", rec->rw, rec->rh);
    }
  }

  if (rec) {
    uint32_t w = width, h = height;
    uint32_t rw = rec->rw, rh = rec->rh;
    const bool zc = rec->dmabuf;

    // App capture: nothing can have changed in the recorded window unless
    // it committed since the last frame we took, so drop this present
    // rather than encode a byte-identical copy. Bounded — after
    // kRecKeepaliveUs a frame goes out regardless, so a session that ends
    // during a long still stretch still carries the right duration.
    if (rec->cb && g_api_record_texture.load(std::memory_order_relaxed) >= 0) {
      struct timespec sk;
      clock_gettime(CLOCK_MONOTONIC, &sk);
      const uint64_t now_us =
          (uint64_t)sk.tv_sec * 1000000ull + sk.tv_nsec / 1000;
      const uint64_t epoch =
          g_api_record_epoch.load(std::memory_order_acquire);
      if (rec->last_frame_us != 0 && epoch == rec->last_epoch &&
          now_us - rec->last_frame_us < kRecKeepaliveUs) {
        rec->skipped++;
        return;
      }
      rec->last_epoch = epoch;
      rec->last_frame_us = now_us;
    }

    // Zero-copy: claim a free ring slot before touching any GL — every
    // slot still held by the encoder means it has fallen behind, and the
    // right response is to drop this frame, not stall the present.
    //
    // The search ROTATES. Taking the first free slot every time looks
    // harmless — the encoder usually has released slot 0 by the next
    // present, so slot 0 is free and gets picked again — but then the
    // compositor's blit into slot 0 and the encoder's read of slot 0 are
    // the same buffer one frame apart, and the kernel's implicit dma-buf
    // fencing serializes them into a lockstep round trip: blit, encode,
    // wait, blit. Measured, that pinned presents to every third vsync
    // (100ms on a 30Hz panel) no matter how small the capture, so a
    // 660x948 window recorded at the same 10fps as the whole 4K screen.
    // Rotating hands the compositor a different buffer than the one being
    // read, which is the entire point of having a ring.
    static int zc_cursor = 0;
    int zc_slot = -1;
    if (zc) {
      uint32_t busy = g_dmabuf_busy.load(std::memory_order_acquire);
      for (int i = 0; i < kDmabufRing; i++) {
        int cand = (zc_cursor + i) % kDmabufRing;
        if (!(busy & (1u << cand))) { zc_slot = cand; break; }
      }
      if (zc_slot < 0) {
        rec->dropped++;
        return;
      }
      zc_cursor = (zc_slot + 1) % kDmabufRing;
    }

    static GLuint rec_fbo = 0;
    static uint32_t fbo_w = 0, fbo_h = 0;
    if (!zc) {
      if (rec_fbo == 0 || fbo_w != rw || fbo_h != rh) {
        if (rec_fbo == 0) glGenFramebuffers(1, &rec_fbo);
        GLuint rbo = 0;
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_, rw, rh);
        glBindFramebuffer(GL_FRAMEBUFFER, rec_fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, rbo);
        fbo_w = rw;
        fbo_h = rh;
      }
      if (rec_pbo[0] == 0) {
        glGenBuffers(kRecRing, rec_pbo);
      }
      // Sized for THIS session — the debug and API sinks capture at
      // different resolutions, so the ring can't be allocated once and
      // forgotten.
      if (rec_pbo_bytes != (size_t)rw * rh * 4) {
        for (int i = 0; i < kRecRing; i++) {
          glBindBuffer(GL_PIXEL_PACK_BUFFER_, rec_pbo[i]);
          glBufferData(GL_PIXEL_PACK_BUFFER_, (GLsizeiptr)rw * rh * 4,
                       nullptr, GL_STREAM_READ_);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
        rec_pbo_bytes = (size_t)rw * rh * 4;
      }

      // The slot queued kRecRing presents ago is done — collect it first,
      // then reuse it for this frame. The caller's READ framebuffer binding
      // is the capture source; re-bind it after the downsample blit.
      drain_slot(rec_slot);
    }

    // The capture source: the whole output, or the live crop (a window
    // recording — the shell re-points it as the window moves). Top-down
    // crop coords; GL rows are bottom-up, so flip while cropping. Clamped
    // so a window half-dragged off screen doesn't blit garbage.
    int sx = 0, sy = 0;
    int sw = (int)w, sh = (int)h;
    if (rec->cb && g_api_crop[2].load(std::memory_order_relaxed) > 0) {
      sx = std::min(std::max(g_api_crop[0].load(), 0), (int)w - 1);
      sy = std::min(std::max(g_api_crop[1].load(), 0), (int)h - 1);
      sw = std::min(std::max(g_api_crop[2].load(), 1), (int)w - sx);
      sh = std::min(std::max(g_api_crop[3].load(), 1), (int)h - sy);
    }

    // True app capture: resolve the window's texture the way compositing
    // does (same callback, same raster thread) and read THAT instead of
    // the framebuffer. The resolve also refreshes a dirty client texture,
    // so content stays live even while the window is minimized or covered.
    int64_t src_tex_id = rec->cb
        ? g_api_record_texture.load(std::memory_order_relaxed) : -1;
    GLuint src_tex_name = 0;
    int tw = 0, th = 0;
    if (src_tex_id >= 0) {
      FlutterOpenGLTexture tex = {};
      if (v->external_texture_callback &&
          v->external_texture_callback(v->external_texture_user_data,
                                       src_tex_id, 0, 0, &tex) &&
          tex.name != 0) {
        src_tex_name = (GLuint)tex.name;
        tw = (int)tex.width;
        th = (int)tex.height;
      } else {
        // Window texture unresolvable this present — skip the frame (no
        // bindings touched yet); the shell ends the session when the
        // window is truly gone.
        return;
      }
    }

    const GLuint target_fbo = zc ? g_dmabuf_ring.slots[zc_slot].fbo : rec_fbo;
    GLint read_fbo_binding = 0;
    glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */,
                  &read_fbo_binding);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER_, target_fbo);
    if (src_tex_name != 0 && tw > 0 && th > 0) {
      static GLuint src_fbo = 0;
      if (src_fbo == 0) glGenFramebuffers(1, &src_fbo);
      glBindFramebuffer(GL_READ_FRAMEBUFFER_, src_fbo);
      glFramebufferTexture2D(GL_READ_FRAMEBUFFER_, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, src_tex_name, 0);
      // Wayland client buffers are top-down (straight blit); first-party
      // children render into GL FBOs, bottom-up (flip) — the same split
      // the scene handles as flipTextureY.
      if (g_api_record_tex_topdown.load(std::memory_order_relaxed)) {
        es3.blit(0, 0, tw, th, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      } else {
        es3.blit(0, th, tw, 0, 0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
      }
    } else {
      // Flipped source rect: GL rows are bottom-up, output is top-down.
      es3.blit(sx, (int)h - sy, sx + sw, (int)h - sy - sh,
               0, 0, rw, rh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t ts_us =
        (uint64_t)ts.tv_sec * 1000000ull + ts.tv_nsec / 1000;
    // Cursor state travels with the frame it was captured alongside; it is
    // painted in (GPU or writer thread) only when the pointer is on this
    // output. Crop-relative for window recordings (exact while the crop
    // holds its start size; a mid-recording resize skews it with the
    // scaling).
    flutter::FlCursorSnapshot cur = v->cursor.Snapshot();
    if (cur.crtc_id != v->display.crtc_id()) {
      cur.visible = false;
    }
    if (src_tex_id >= 0) {
      // App capture is window-space; the cursor lives in screen-space.
      cur.visible = false;
    }
    cur.x -= sx;
    cur.y -= sy;

    if (zc) {
      // The frame is already in shareable memory — draw the cursor on the
      // GPU, submit, and hand the fd over. glFlush only queues the work;
      // ordering against the encoder's reads rides the kernel's implicit
      // dma-buf fencing (plus the ring: a slot comes back long before its
      // turn recurs).
      if (cur.visible) DrawCursorGpu(cur, rw, rh, rec->shift);
      glFlush();
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER_, read_fbo_binding);
      const DmabufSlot& s = g_dmabuf_ring.slots[zc_slot];
      FlDrmRecordDmabufFrame fr = {};
      fr.slot = zc_slot;
      fr.fd = s.fd;
      fr.width = rw;
      fr.height = rh;
      fr.stride = s.stride;
      fr.offset = 0;
      fr.fourcc = DRM_FORMAT_ABGR8888_;
      fr.modifier = s.modifier;
      fr.timestamp_us = ts_us;
      g_dmabuf_busy.fetch_or(1u << zc_slot, std::memory_order_acq_rel);
      v->record_dmabuf_callback(v->record_dmabuf_user_data, &fr);
    } else {
      glBindFramebuffer(GL_READ_FRAMEBUFFER_, rec_fbo);
      glBindBuffer(GL_PIXEL_PACK_BUFFER_, rec_pbo[rec_slot]);
      glReadPixels(0, 0, rw, rh, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
      glBindBuffer(GL_PIXEL_PACK_BUFFER_, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glBindFramebuffer(GL_READ_FRAMEBUFFER_, read_fbo_binding);
      rec_pbo_ts[rec_slot] = ts_us;
      rec_pbo_cursor[rec_slot] = cur;
      rec_pbo_pending[rec_slot] = true;
      rec_slot = (rec_slot + 1) % kRecRing;
    }
  }
}

// ─── FlutterCompositor callbacks (multi-view present path) ───────────────────

// One backing store: an FBO the engine renders a view's layer tree into.
// Created/collected on the raster thread with the render context current.
struct FlDrmBackingStore {
  GLuint fbo = 0;
  GLuint texture = 0;
  GLuint depth_stencil = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

static bool CompositorCreateBackingStore(
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out,
    void* user_data) {
  uint32_t width = (uint32_t)config->size.width;
  uint32_t height = (uint32_t)config->size.height;
  auto* store = new FlDrmBackingStore();
  store->width = width;
  store->height = height;

  glGenTextures(1, &store->texture);
  glBindTexture(GL_TEXTURE_2D, store->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Depth+stencil so Skia clipping never depends on the wrapped FBO lacking
  // attachments (mirrors the GTK embedder's framebuffers).
  glGenRenderbuffers(1, &store->depth_stencil);
  glBindRenderbuffer(GL_RENDERBUFFER, store->depth_stencil);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_, width, height);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glGenFramebuffers(1, &store->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, store->fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         store->texture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT_,
                            GL_RENDERBUFFER, store->depth_stencil);
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "[DrmView] Backing store FBO incomplete: 0x%x (%ux%u)\n",
            status, width, height);
    glDeleteFramebuffers(1, &store->fbo);
    glDeleteTextures(1, &store->texture);
    glDeleteRenderbuffers(1, &store->depth_stencil);
    delete store;
    return false;
  }

  backing_store_out->type = kFlutterBackingStoreTypeOpenGL;
  backing_store_out->user_data = store;
  backing_store_out->open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
  // Misnamed field: "target" is the color format.
  backing_store_out->open_gl.framebuffer.target = GL_RGBA8_;
  backing_store_out->open_gl.framebuffer.name = store->fbo;
  backing_store_out->open_gl.framebuffer.user_data = store;
  // GL resources are freed in the collect callback (raster thread, context
  // current); the struct-destruction callback has no such guarantee.
  backing_store_out->open_gl.framebuffer.destruction_callback = [](void*) {};
  return true;
}

static bool CompositorCollectBackingStore(
    const FlutterBackingStore* backing_store,
    void* user_data) {
  auto* store = static_cast<FlDrmBackingStore*>(
      backing_store->open_gl.framebuffer.user_data);
  if (store) {
    glDeleteFramebuffers(1, &store->fbo);
    glDeleteTextures(1, &store->texture);
    glDeleteRenderbuffers(1, &store->depth_stencil);
    delete store;
  }
  return true;
}

// Raster thread: blit the view's layers onto the mapped output's window
// surface, run capture hooks (primary only), page-flip that output's chain.
static bool CompositorPresentView(const FlutterPresentViewInfo* info) {
  auto* view = static_cast<FlDrmView*>(info->user_data);

  size_t output_index;
  {
    std::lock_guard<std::mutex> lock(view->view_map_mutex);
    auto it = view->view_to_output.find(info->view_id);
    if (it == view->view_to_output.end()) {
      return true;  // view without an output (shouldn't happen) — drop frame
    }
    output_index = it->second;
  }
  auto& res = view->output_resources[output_index];
  if (!res.chain) {
    return true;  // output was disabled at init — drop frame
  }

  const FlDrmOutput& out = view->display.output(output_index);
  const uint32_t surf_w = out.width();
  const uint32_t surf_h = out.height();

  // One-time per-view log: proves the app→engine→output pipeline for a view.
  // Raster-thread only, so no locking.
  static std::map<int64_t, bool> s_view_present_logged;
  const bool first_present = !s_view_present_logged[info->view_id];
  if (first_present) {
    s_view_present_logged[info->view_id] = true;
    fprintf(stderr, "[DrmView] view %lld first present (%zu layers -> %s)\n",
            (long long)info->view_id, info->layers_count, out.name);
  }

  // Bind the render context to THIS output's window surface. The engine's
  // make_current callback re-binds the primary whenever it needs it; Skia
  // renders into backing-store FBOs, so the draw surface only matters here.
  view->egl.MakeCurrent(res.egl_surface);
  glDisable(GL_SCISSOR_TEST);

  glBindFramebuffer(GL_DRAW_FRAMEBUFFER_, 0);
  for (size_t i = 0; i < info->layers_count; i++) {
    const FlutterLayer* layer = info->layers[i];
    if (layer->type != kFlutterLayerContentTypeBackingStore) {
      continue;  // no engine-level platform views in this embedder
    }
    GLuint src_fbo = layer->backing_store->open_gl.framebuffer.name;
    const int lw = (int)layer->size.width;
    const int lh = (int)layer->size.height;
    const int dx = (int)layer->offset.x;
    // Layer offsets are from the top-left; GL framebuffers are bottom-left.
    const int dy = (int)surf_h - ((int)layer->offset.y + lh);
    glBindFramebuffer(GL_READ_FRAMEBUFFER_, src_fbo);
    GetEs3Fns().blit(0, 0, lw, lh, dx, dy, dx + lw, dy + lh,
                     GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }

  if (output_index == view->display.primary_index()) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER_, 0);
    RunCaptureHooks(view, surf_w, surf_h);
  } else if (const char* dbg = getenv("STARLING_DEBUG_CAPTURE");
             first_present || (dbg && dbg[0]) || g_secondary_screenshot) {
    // Headless outputs can't be eyeballed — print pixel stats of the first
    // frame (every frame under STARLING_DEBUG_CAPTURE) so per-view content
    // is verifiable from the log, and dump a PPM when a screenshot was
    // requested (SIGUSR1 captures EVERY output, not just the primary).
    glBindFramebuffer(GL_READ_FRAMEBUFFER_, 0);
    uint8_t* px = new uint8_t[(size_t)surf_w * surf_h * 4];
    glReadPixels(0, 0, surf_w, surf_h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    size_t nonblack = 0, n = (size_t)surf_w * surf_h;
    for (size_t p = 0; p < n; p++) {
      if (px[p * 4] > 24 || px[p * 4 + 1] > 24 || px[p * 4 + 2] > 24) {
        nonblack++;
      }
    }
    size_t c = (n / 2 + surf_w / 2) * 4;
    fprintf(stderr,
            "[DrmView] view %lld frame0 on %s: nonblack=%.3f "
            "center_rgba=%u,%u,%u,%u\n",
            (long long)info->view_id, out.name, (double)nonblack / (double)n,
            px[c], px[c + 1], px[c + 2], px[c + 3]);
    if (g_secondary_screenshot) {
      g_secondary_screenshot = 0;
      char path[128];
      snprintf(path, sizeof(path), "/tmp/drm_screenshot_%s.ppm", out.name);
      FILE* f = fopen(path, "wb");
      if (f) {
        fprintf(f, "P6\n%u %u\n255\n", surf_w, surf_h);
        for (int y = (int)surf_h - 1; y >= 0; y--) {
          for (uint32_t x = 0; x < surf_w; x++) {
            fwrite(px + ((size_t)y * surf_w + x) * 4, 1, 3, f);
          }
        }
        fclose(f);
        fprintf(stderr, "[DrmView] Screenshot saved to %s\n", path);
      }
    }
    delete[] px;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return res.chain->Present();
}

// Rebuild the input layer's pointer regions from the placed outputs that
// have Flutter views (the primary is the implicit view 0).
static void RebuildInputRegions(FlDrmView* view) {
  std::vector<FlInputRegion> regions;
  for (size_t i = 0; i < view->display.num_outputs(); i++) {
    if (i >= view->placements.size() || !view->placements[i].placed) {
      continue;
    }
    int64_t view_id = -1;
    if (i == view->display.primary_index()) {
      view_id = 0;
    } else {
      std::lock_guard<std::mutex> lock(view->view_map_mutex);
      for (const auto& entry : view->view_to_output) {
        if (entry.second == i) {
          view_id = entry.first;
          break;
        }
      }
    }
    if (view_id < 0) {
      continue;  // no Flutter view renders it — pointer skips this output
    }
    const auto& placement = view->placements[i];
    const FlDrmOutput& out = view->display.output(i);
    FlInputRegion region;
    region.logical_x = placement.logical_x;
    region.logical_y = placement.logical_y;
    region.scale = placement.scale;
    region.logical_w = out.width() / placement.scale;
    region.logical_h = out.height() / placement.scale;
    region.view_id = view_id;
    region.crtc_id = out.crtc_id;
    regions.push_back(region);
  }
  view->input.SetRegions(regions);
}

// Platform thread: tear down a removed output's resources. Only called once
// the engine has stopped presenting its view (RemoveView completed) — or
// when the output never had a view.
static void CompleteOutputTeardown(FlDrmView* view,
                                   size_t index,
                                   int64_t view_id) {
  if (view_id > 0) {
    std::lock_guard<std::mutex> lock(view->view_map_mutex);
    view->view_to_output.erase(view_id);
  }
  if (index >= view->output_resources.size()) {
    return;
  }
  auto& res = view->output_resources[index];
  if (res.chain) {
    // Wait briefly for an in-flight flip's event: its handler holds the
    // chain pointer, so deleting under it would be a use-after-free. On a
    // dead connector the event may never come — leak the chain then.
    for (int tries = 0; res.chain->waiting_for_flip() && tries < 4; tries++) {
      struct pollfd pfd = {view->display.fd(), POLLIN, 0};
      if (poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLIN)) {
        FlDrmSwapChain::DrainDrmEvents(view->display.fd());
      }
    }
    drmModeSetCrtc(view->display.fd(), view->display.output(index).crtc_id,
                   0, 0, 0, nullptr, 0, nullptr);
    if (res.chain->waiting_for_flip()) {
      fprintf(stderr,
              "[DrmView] output %zu: flip event never arrived — leaking its "
              "swap chain\n",
              index);
    } else {
      delete res.chain;
    }
    res.chain = nullptr;
  }
  if (res.egl_surface != EGL_NO_SURFACE) {
    view->egl.DestroyWindowSurface(res.egl_surface);
    res.egl_surface = EGL_NO_SURFACE;
  }
  if (res.gbm_surface_ptr) {
    view->gbm.DestroySurface(res.gbm_surface_ptr);
    res.gbm_surface_ptr = nullptr;
  }
  fprintf(stderr, "[DrmView] output %zu (view %lld) torn down\n", index,
          (long long)view_id);
}

// Platform thread: a connector disconnected. Take its output out of the
// pointer space immediately, then remove its Flutter view; the resource
// teardown completes via pending_teardowns once the engine confirms it has
// stopped presenting the view.
static void BeginOutputRemoval(FlDrmView* view, size_t index) {
  if (index < view->placements.size()) {
    view->placements[index].placed = false;
  }
  RebuildInputRegions(view);

  int64_t view_id = -1;
  {
    std::lock_guard<std::mutex> lock(view->view_map_mutex);
    for (const auto& entry : view->view_to_output) {
      if (entry.second == index && entry.first != 0) {
        view_id = entry.first;
        break;
      }
    }
  }
  if (view_id <= 0) {
    CompleteOutputTeardown(view, index, view_id);
    return;
  }

  struct RemovalBaton {
    FlDrmView* view;
    size_t index;
    int64_t view_id;
  };
  auto* baton = new RemovalBaton{view, index, view_id};
  FlutterRemoveViewInfo info = {};
  info.struct_size = sizeof(FlutterRemoveViewInfo);
  info.view_id = view_id;
  info.user_data = baton;
  info.remove_view_callback = [](const FlutterRemoveViewResult* result) {
    // Engine-internal thread: queue for the platform loop.
    auto* b = static_cast<RemovalBaton*>(result->user_data);
    fprintf(stderr, "[DrmView] RemoveView %lld -> %s\n",
            (long long)b->view_id, result->removed ? "removed" : "FAILED");
    {
      std::lock_guard<std::mutex> lock(b->view->teardown_mutex);
      b->view->pending_teardowns.push_back({b->index, b->view_id});
    }
    delete b;
  };
  fprintf(stderr, "[DrmView] Removing view %lld (output %zu)\n",
          (long long)view_id, index);
  if (FlutterEngineRemoveView(view->engine, &info) != kSuccess) {
    fprintf(stderr, "[DrmView] FlutterEngineRemoveView(%lld) rejected\n",
            (long long)view_id);
    delete baton;
    CompleteOutputTeardown(view, index, view_id);
  }
}

// Platform thread: a DRM connector changed. Rescan; tear down what left,
// give every NEW output its scanout pipeline (surface + chain + black
// modeset via the dedicated modeset context), then hand policy to the app
// callback — views, layout, and wl_output advertisement are the app's call.
// Idempotent: repeated uevents for the same state change nothing.
static void HandleDrmHotplug(FlDrmView* view) {
  std::vector<size_t> added;
  std::vector<size_t> removed;
  view->display.RescanConnectors(&added, &removed);
  for (size_t index : removed) {
    BeginOutputRemoval(view, index);
  }
  if (added.empty()) {
    if (!removed.empty() && view->outputs_changed_callback) {
      view->outputs_changed_callback(view->outputs_changed_user_data);
    }
    return;
  }
  if (view->output_resources.size() < view->display.num_outputs()) {
    view->output_resources.resize(view->display.num_outputs());
  }
  if (view->placements.size() < view->display.num_outputs()) {
    view->placements.resize(view->display.num_outputs());
  }
  bool any_ready = false;
  for (size_t i : added) {
    const FlDrmOutput& out = view->display.output(i);
    auto& res = view->output_resources[i];
    if (res.chain) {
      // Reused slot whose previous teardown hasn't completed (plugged
      // faster than the engine released the old view) — skip; the next
      // uevent will pick it up.
      fprintf(stderr, "[DrmView] hotplug: slot %zu busy (teardown pending)\n",
              i);
      continue;
    }
    res.gbm_surface_ptr =
        view->gbm.CreateScanoutSurface(out.width(), out.height());
    if (res.gbm_surface_ptr) {
      res.egl_surface = view->egl.CreateWindowSurface(res.gbm_surface_ptr);
    }
    if (res.egl_surface == EGL_NO_SURFACE) {
      fprintf(stderr, "[DrmView] hotplug: %s surface init failed\n",
              out.name);
      continue;
    }
    res.chain = new FlDrmSwapChain(&view->display, &view->egl, out,
                                   res.gbm_surface_ptr, res.egl_surface);
    res.chain->set_vt_active(&view->vt_active);
    if (!res.chain->InitialModeSet(view->modeset_context)) {
      fprintf(stderr, "[DrmView] hotplug: %s mode set failed\n", out.name);
      delete res.chain;
      res.chain = nullptr;
      continue;
    }
    any_ready = true;
  }
  if ((any_ready || !removed.empty()) && view->outputs_changed_callback) {
    view->outputs_changed_callback(view->outputs_changed_user_data);
  }
}

// ─── Public C API ────────────────────────────────────────────────────────────

extern "C" {

FlDrmView* fl_drm_view_create(const char* assets_path,
                                const char* icu_data_path,
                                void* runtime_controller) {
  auto* view = new FlDrmView();

  // Initialize UI task wakeup pipe.
  {
    int fds[2];
    if (pipe(fds) == 0) {
      view->ui_wakeup_read_fd = fds[0];
      view->ui_wakeup_write_fd = fds[1];
      // Set read end non-blocking
      int flags = fcntl(fds[0], F_GETFL);
      fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
    }
  }

  // 0. Resolve seat access: libseat (logind/seatd session, unprivileged) or
  // direct device opens (root dev workflow). Must precede every device open.
  if (!view->seat.Open()) {
    fprintf(stderr, "[DrmView] Seat init failed\n");
    delete view;
    return nullptr;
  }

  // 1. Initialize DRM display.
  if (!view->display.Initialize(&view->seat)) {
    fprintf(stderr, "[DrmView] Display init failed\n");
    delete view;
    return nullptr;
  }

  uint32_t width = view->display.width();
  uint32_t height = view->display.height();

  // 1b. Initialize VT handling (non-fatal if it fails). In libseat mode the
  // seat manager owns the VT — graphics mode, keyboard mute, and switch
  // handshakes all arrive as seat enable/disable events instead.
  if (view->seat.via_libseat()) {
    fprintf(stderr, "[VT] Managed by seat manager (%s)\n",
            view->seat.seat_name());
  } else {
    int tty_fd = -1;
    int vt_num = 0;
    const char* vt_env = getenv("FLUTTER_VT");

    if (vt_env) {
      // Explicit VT number requested.
      vt_num = atoi(vt_env);
      if (vt_num > 0) {
        char tty_path[32];
        snprintf(tty_path, sizeof(tty_path), "/dev/tty%d", vt_num);
        tty_fd = open(tty_path, O_RDWR | O_CLOEXEC);
        if (tty_fd >= 0) {
          ioctl(tty_fd, VT_ACTIVATE, vt_num);
          ioctl(tty_fd, VT_WAITACTIVE, vt_num);
        }
      }
    } else {
      // Use current controlling terminal.
      tty_fd = open("/dev/tty", O_RDWR | O_CLOEXEC);
      if (tty_fd < 0) {
        // No controlling terminal — try to allocate a free VT.
        int tty0 = open("/dev/tty0", O_RDWR | O_CLOEXEC);
        if (tty0 >= 0) {
          if (ioctl(tty0, VT_OPENQRY, &vt_num) == 0 && vt_num > 0) {
            close(tty0);
            char tty_path[32];
            snprintf(tty_path, sizeof(tty_path), "/dev/tty%d", vt_num);
            tty_fd = open(tty_path, O_RDWR | O_CLOEXEC);
            if (tty_fd >= 0) {
              ioctl(tty_fd, VT_ACTIVATE, vt_num);
              ioctl(tty_fd, VT_WAITACTIVE, vt_num);
            }
          } else {
            close(tty0);
          }
        }
      } else {
        // Get current VT number.
        struct vt_stat vs = {};
        if (ioctl(tty_fd, VT_GETSTATE, &vs) == 0)
          vt_num = vs.v_active;
      }
    }

    if (tty_fd >= 0) {
      // Try to claim DRM master now that our VT is active.
      // May already have it (first shell) or may get it now
      // (second shell, after the first dropped master via VT switch).
      drmSetMaster(view->display.fd());

      // Switch to graphics mode (suppress kernel text console).
      ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);

      // Take the keyboard away from the kernel console. Without K_OFF the
      // console keymap still fires on every keystroke — Alt+Left/Right
      // switch VTs out from under the shell. Ctrl+Alt+Fn switching is
      // re-implemented in FlDrmInput (fl_drm_request_vt_switch).
      if (ioctl(tty_fd, KDGKBMODE, &view->vt_saved_kb_mode) != 0)
        view->vt_saved_kb_mode = -1;
      ioctl(tty_fd, KDSKBMODE, K_OFF);

      // VT_PROCESS mode — kernel sends us signals instead of switching directly.
      struct vt_mode vtm = {};
      vtm.mode = VT_PROCESS;
      vtm.relsig = SIGUSR2;
      vtm.acqsig = SIGRTMIN;
      ioctl(tty_fd, VT_SETMODE, &vtm);

      view->vt_tty_fd = tty_fd;
      view->vt_num = vt_num;
      view->vt_initialized = true;

      // Set globals for signal handlers.
      g_vt_tty_fd = tty_fd;
      g_vt_drm_fd = view->display.fd();
      g_vt_active_ptr = &view->vt_active;

      fprintf(stderr, "[VT] Initialized on VT%d\n", vt_num);
    } else {
      fprintf(stderr, "[VT] No VT available (non-fatal)\n");
    }
  }

  // 2. Initialize GBM device.
  if (!view->gbm.Initialize(view->display.fd())) {
    fprintf(stderr, "[DrmView] GBM init failed\n");
    delete view;
    return nullptr;
  }

  // 3. Initialize EGL (display, config, contexts — surfaces come per output).
  if (!view->egl.Initialize(view->gbm.device())) {
    fprintf(stderr, "[DrmView] EGL init failed\n");
    delete view;
    return nullptr;
  }

  // 4. Per-output scanout surfaces + swap chains. The primary drives the
  // engine; a failed secondary is skipped (its CRTC just stays off).
  // Reserve so hotplug-time appends never move entries other threads
  // index into (present_view on the raster thread).
  size_t primary_index = view->display.primary_index();
  view->output_resources.reserve(16);
  view->placements.reserve(16);
  view->output_resources.resize(view->display.num_outputs());
  for (size_t i = 0; i < view->display.num_outputs(); i++) {
    const FlDrmOutput& out = view->display.output(i);
    auto& res = view->output_resources[i];
    res.gbm_surface_ptr =
        view->gbm.CreateScanoutSurface(out.width(), out.height());
    if (res.gbm_surface_ptr) {
      res.egl_surface = view->egl.CreateWindowSurface(res.gbm_surface_ptr);
    }
    if (res.egl_surface == EGL_NO_SURFACE) {
      if (i == primary_index) {
        fprintf(stderr, "[DrmView] Primary surface init failed\n");
        delete view;
        return nullptr;
      }
      fprintf(stderr, "[DrmView] Skipping output %s (surface init failed)\n",
              out.name);
      continue;
    }
    if (i == primary_index) {
      view->egl.set_primary_surface(res.egl_surface);
    }
    res.chain = new FlDrmSwapChain(&view->display, &view->egl, out,
                                   res.gbm_surface_ptr, res.egl_surface);
    res.chain->set_vt_active(&view->vt_active);
  }
  view->swap_chain = view->output_resources[primary_index].chain;

  // Refresh period from the primary's mode (vrefresh is integer Hz).
  {
    uint32_t vrefresh = view->display.mode().vrefresh;
    view->refresh_ns = vrefresh > 0 ? 1000000000u / vrefresh : 0;
  }
  // Bridge PRIMARY flip completions to the public present callback — the
  // compositor paces Wayland clients off it, so secondary outputs' flips
  // must not fire it (per-output pacing comes with per-output views).
  view->swap_chain->set_present_callback(
      [](void* ud, uint64_t flip_time_ns) {
        auto* v = static_cast<FlDrmView*>(ud);
        if (v->present_callback) {
          v->present_callback(v->present_callback_user_data, flip_time_ns,
                              v->refresh_ns);
        }
      },
      view);

  // Initial modeset on every output (secondaries show black until they get
  // their own Flutter views).
  for (size_t i = 0; i < view->output_resources.size(); i++) {
    auto& res = view->output_resources[i];
    if (!res.chain) {
      continue;
    }
    if (!res.chain->InitialModeSet()) {
      if (i == primary_index) {
        fprintf(stderr, "[DrmView] Initial mode set failed\n");
        delete view;
        return nullptr;
      }
      fprintf(stderr, "[DrmView] Disabling output %s (mode set failed)\n",
              view->display.output(i).name);
      delete res.chain;
      res.chain = nullptr;
    }
  }

  // 4b. Compositor path decision. Default on; FLUTTER_DRM_COMPOSITOR=0 (or a
  // context without ES3 blit) falls back to the direct single-view present.
  {
    const char* comp_env = getenv("FLUTTER_DRM_COMPOSITOR");
    view->use_compositor = !(comp_env && comp_env[0] == '0');
    if (view->use_compositor) {
      view->egl.MakeCurrent();
      bool have_blit = GetEs3Fns().blit != nullptr;
      view->egl.ClearCurrent();
      if (!have_blit) {
        fprintf(stderr,
                "[DrmView] No ES3 glBlitFramebuffer — compositor path off\n");
        view->use_compositor = false;
      }
    }
    // The implicit view renders to the primary output in both paths.
    view->view_to_output[0] = primary_index;
  }

  // 4c. Hotplug plumbing: a context for hotplug-time modesets (the render
  // context belongs to the raster thread once the engine runs) and a udev
  // monitor for DRM connector-change events (read in the platform loop).
  view->modeset_context = view->egl.CreateAuxContext();
  view->hotplug_udev = udev_new();
  if (view->hotplug_udev) {
    view->hotplug_monitor =
        udev_monitor_new_from_netlink(view->hotplug_udev, "udev");
    if (view->hotplug_monitor) {
      udev_monitor_filter_add_match_subsystem_devtype(view->hotplug_monitor,
                                                      "drm", nullptr);
      udev_monitor_enable_receiving(view->hotplug_monitor);
      fprintf(stderr, "[DrmView] Hotplug monitor active\n");
    }
  }

  // The primary's pixel ratio: used for its window metrics and its pointer
  // region (event coords stay physical primary pixels either way).
  double primary_pixel_ratio = 1.0;
  {
    const char* dpi_env = getenv("FLUTTER_DRM_DPI");
    primary_pixel_ratio = dpi_env ? atof(dpi_env) : 1.0;
    if (primary_pixel_ratio < 0.5) primary_pixel_ratio = 0.5;
    if (primary_pixel_ratio > 4.0) primary_pixel_ratio = 4.0;
  }

  // 5. Initialize input.
  if (!view->input.Initialize(width, height, &view->seat)) {
    fprintf(stderr, "[DrmView] Input init failed (non-fatal)\n");
  }
  // Default virtual-desktop placement: the primary at (0,0). Secondaries
  // join the pointer space when the app places them (set_output_layout).
  view->placements.resize(view->display.num_outputs());
  view->placements[primary_index].placed = true;
  view->placements[primary_index].scale = primary_pixel_ratio;
  RebuildInputRegions(view);

  // 6. Initialize cursor.
  if (!view->cursor.Initialize(view->display.fd(), view->display.crtc_id(),
                                view->gbm.device())) {
    fprintf(stderr, "[DrmView] Cursor init failed (non-fatal)\n");
  }

  // 7. Set up task runner.
  view->task_runner.SetPlatformThread();

  // 8. Build Flutter renderer config.
  FlutterRendererConfig config = {};
  config.type = kOpenGL;
  config.open_gl.struct_size = sizeof(config.open_gl);

  config.open_gl.make_current = [](void* ud) -> bool {
    return static_cast<FlDrmView*>(ud)->egl.MakeCurrent();
  };
  config.open_gl.clear_current = [](void* ud) -> bool {
    return static_cast<FlDrmView*>(ud)->egl.ClearCurrent();
  };
  // Use present_with_info (not legacy present) to receive frame/buffer
  // damage for partial repaint tracking.
  config.open_gl.present = nullptr;
  config.open_gl.present_with_info =
      [](void* ud, const FlutterPresentInfo* info) -> bool {
    auto* v = static_cast<FlDrmView*>(ud);

    // Save frame_damage for the next frame's existing_damage.
    // Shift history: [0] = most recent, [1] = one before that.
    v->prev_damage[1] = v->prev_damage[0];
    if (info->frame_damage.num_rects > 0 && info->frame_damage.damage) {
      v->prev_damage[0] = info->frame_damage.damage[0];
    } else {
      // No damage info — assume full frame.
      v->prev_damage[0] = {0, 0, (double)v->display.width(),
                           (double)v->display.height()};
    }
    v->damage_history_count++;

    // Capture hooks: screenshot / recording / first-frame debug stats,
    // reading the engine's render target (the window surface, FBO 0).
    RunCaptureHooks(v, v->display.width(), v->display.height());

    return v->swap_chain->Present();
  };
  config.open_gl.fbo_callback = [](void*) -> uint32_t {
    return 0;  // On-screen framebuffer.
  };
  // Report existing damage for partial repaint.  Returns the union of the
  // last 2 frames' frame_damage so the engine knows which areas of the
  // back buffer are stale (handles double and triple buffering).
  config.open_gl.populate_existing_damage =
      [](void* ud, const intptr_t fbo_id, FlutterDamage* damage) {
    auto* v = static_cast<FlDrmView*>(ud);
    if (v->damage_history_count < 2) {
      // Not enough history — force full repaint.
      v->existing_damage_scratch = {0, 0, (double)v->display.width(),
                                    (double)v->display.height()};
    } else {
      // Union of the last 2 frames' damage.
      v->existing_damage_scratch = {
          std::min(v->prev_damage[0].left, v->prev_damage[1].left),
          std::min(v->prev_damage[0].top, v->prev_damage[1].top),
          std::max(v->prev_damage[0].right, v->prev_damage[1].right),
          std::max(v->prev_damage[0].bottom, v->prev_damage[1].bottom),
      };
    }
    damage->struct_size = sizeof(FlutterDamage);
    damage->num_rects = 1;
    damage->damage = &v->existing_damage_scratch;
  };
  config.open_gl.make_resource_current = [](void* ud) -> bool {
    return static_cast<FlDrmView*>(ud)->egl.MakeResourceCurrent();
  };
  config.open_gl.gl_proc_resolver = [](void*, const char* name) -> void* {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
  };
  config.open_gl.gl_external_texture_frame_callback =
      [](void* ud, int64_t texture_id, size_t w, size_t h,
         FlutterOpenGLTexture* out) -> bool {
    auto* v = static_cast<FlDrmView*>(ud);
    if (v->external_texture_callback) {
      return v->external_texture_callback(
          v->external_texture_user_data, texture_id,
          static_cast<int>(w), static_cast<int>(h), out);
    }
    return false;
  };
  config.open_gl.fbo_reset_after_present = true;

  // 9. Build project args.
  FlutterProjectArgs args = {};
  args.struct_size = sizeof(args);
  args.assets_path = assets_path;
  args.icu_data_path = icu_data_path;

  // Pass --enable-impeller=false for Skia (broader GPU compat).
  const char* argv[] = {"flutter_drm", "--enable-impeller=false"};
  args.command_line_argc = 2;
  args.command_line_argv = argv;

  // Compositor: engine renders views into FBO backing stores; present_view
  // routes each view to its output. With it set, the renderer config's
  // present/fbo callbacks above are not used by the engine.
  if (view->use_compositor) {
    view->compositor.struct_size = sizeof(FlutterCompositor);
    view->compositor.user_data = view;
    view->compositor.create_backing_store_callback =
        CompositorCreateBackingStore;
    view->compositor.collect_backing_store_callback =
        CompositorCollectBackingStore;
    view->compositor.present_view_callback = CompositorPresentView;
    view->compositor.avoid_backing_store_cache = false;
    args.compositor = &view->compositor;
    fprintf(stderr, "[DrmView] Compositor path enabled\n");
  }

  // Platform task runner — runs on the epoll background thread.
  view->main_thread = pthread_self();
  FlutterTaskRunnerDescription platform_runner = {};
  platform_runner.struct_size = sizeof(platform_runner);
  platform_runner.user_data = view;
  platform_runner.runs_task_on_current_thread_callback = [](void* ud) -> bool {
    return static_cast<FlDrmView*>(ud)->task_runner.RunsOnCurrentThread();
  };
  platform_runner.post_task_callback = [](FlutterTask task,
                                           uint64_t target_time,
                                           void* ud) {
    static_cast<FlDrmView*>(ud)->task_runner.PostTask(task, target_time);
  };

  // UI task runner — runs on the main thread via GCD main queue.
  FlutterTaskRunnerDescription ui_runner = {};
  ui_runner.struct_size = sizeof(ui_runner);
  ui_runner.user_data = view;
  ui_runner.runs_task_on_current_thread_callback = [](void* ud) -> bool {
    auto* v = static_cast<FlDrmView*>(ud);
    return pthread_equal(pthread_self(), v->main_thread) != 0;
  };
  ui_runner.post_task_callback = [](FlutterTask task,
                                     uint64_t target_time,
                                     void* ud) {
    auto* v = static_cast<FlDrmView*>(ud);
    // Enqueue task for the main thread to drain.
    std::lock_guard<std::mutex> lock(v->ui_mutex);
    v->ui_tasks.push_back({task, target_time});
    // Wake the main thread via wakeup pipe.
    if (v->ui_wakeup_write_fd >= 0) {
      char byte = 1;
      (void)write(v->ui_wakeup_write_fd, &byte, 1);
    }
  };

  FlutterCustomTaskRunners task_runners = {};
  task_runners.struct_size = sizeof(task_runners);
  task_runners.platform_task_runner = &platform_runner;
  task_runners.ui_task_runner = &ui_runner;
  args.custom_task_runners = &task_runners;

  // 10. Initialize and run engine.
  FlutterEngineResult result;

  if (runtime_controller) {
    result = FlutterEngineInitializeSwift(
        FLUTTER_ENGINE_VERSION, &config, &args,
        view,
        static_cast<FlutterRuntimeController>(runtime_controller),
        &view->engine);
  } else {
    result = FlutterEngineInitialize(
        FLUTTER_ENGINE_VERSION, &config, &args,
        view,
        &view->engine);
  }

  if (result != kSuccess || !view->engine) {
    fprintf(stderr, "[DrmView] Engine initialization failed\n");
    delete view;
    return nullptr;
  }

  if (runtime_controller) {
    result = FlutterEngineRunInitializedSwift(view->engine);
  } else {
    result = FlutterEngineRunInitialized(view->engine);
  }

  if (result != kSuccess) {
    fprintf(stderr, "[DrmView] Engine run failed\n");
    FlutterEngineShutdown(view->engine);
    delete view;
    return nullptr;
  }

  // 11. Send initial window metrics.
  FlutterWindowMetricsEvent metrics = {};
  metrics.struct_size = sizeof(metrics);
  metrics.width = width;
  metrics.height = height;
  metrics.pixel_ratio = primary_pixel_ratio;
  metrics.view_id = 0;
  FlutterEngineSendWindowMetricsEvent(view->engine, &metrics);

  g_seat_ptr = &view->seat;
  fprintf(stderr, "[DrmView] Created: %ux%u, engine running\n", width, height);
  return view;
}

uint32_t fl_drm_view_get_output_count(FlDrmView* view) {
  return view ? (uint32_t)view->display.num_outputs() : 0;
}

int fl_drm_view_get_output_info(FlDrmView* view,
                                 uint32_t index,
                                 uint32_t* width,
                                 uint32_t* height,
                                 uint32_t* refresh_mhz,
                                 int* is_primary,
                                 char* name_buf,
                                 uint32_t name_buf_size) {
  if (!view || index >= view->display.num_outputs()) {
    return 0;
  }
  const FlDrmOutput& out = view->display.output(index);
  if (!out.alive) {
    return 0;  // disconnected — the slot survives for index stability
  }
  if (width) {
    *width = out.width();
  }
  if (height) {
    *height = out.height();
  }
  if (refresh_mhz) {
    *refresh_mhz = out.mode.vrefresh * 1000;
  }
  if (is_primary) {
    *is_primary = index == view->display.primary_index() ? 1 : 0;
  }
  if (name_buf && name_buf_size > 0) {
    snprintf(name_buf, name_buf_size, "%s", out.name);
  }
  return 1;
}

int fl_drm_view_add_output_view(FlDrmView* view,
                                 uint32_t index,
                                 int64_t flutter_view_id,
                                 double pixel_ratio) {
  if (!view || !view->engine || flutter_view_id == 0 ||
      index >= view->display.num_outputs()) {
    return 0;
  }
  if (!view->use_compositor) {
    fprintf(stderr,
            "[DrmView] add_output_view needs the compositor path (on by "
            "default; unset FLUTTER_DRM_COMPOSITOR=0)\n");
    return 0;
  }
  if (index == view->display.primary_index()) {
    fprintf(stderr,
            "[DrmView] add_output_view: primary is the implicit view 0\n");
    return 0;
  }
  if (!view->output_resources[index].chain) {
    fprintf(stderr, "[DrmView] add_output_view: output %u is disabled\n",
            index);
    return 0;
  }
  const FlDrmOutput& out = view->display.output(index);
  {
    std::lock_guard<std::mutex> lock(view->view_map_mutex);
    for (const auto& entry : view->view_to_output) {
      if (entry.first == flutter_view_id || entry.second == index) {
        fprintf(stderr,
                "[DrmView] add_output_view: view %lld / output %u already "
                "mapped\n",
                (long long)flutter_view_id, index);
        return 0;
      }
    }
    view->view_to_output[flutter_view_id] = index;
  }

  if (pixel_ratio < 0.5) pixel_ratio = 0.5;
  if (pixel_ratio > 4.0) pixel_ratio = 4.0;
  FlutterWindowMetricsEvent view_metrics = {};
  view_metrics.struct_size = sizeof(view_metrics);
  view_metrics.width = out.width();
  view_metrics.height = out.height();
  view_metrics.pixel_ratio = pixel_ratio;
  view_metrics.view_id = flutter_view_id;
  FlutterAddViewInfo add_info = {};
  add_info.struct_size = sizeof(FlutterAddViewInfo);
  add_info.view_id = flutter_view_id;
  add_info.view_metrics = &view_metrics;
  add_info.user_data =
      reinterpret_cast<void*>(static_cast<intptr_t>(flutter_view_id));
  add_info.add_view_callback = [](const FlutterAddViewResult* result) {
    fprintf(stderr, "[DrmView] AddView %lld -> %s\n",
            (long long)(intptr_t)result->user_data,
            result->added ? "added" : "FAILED");
  };
  fprintf(stderr,
          "[DrmView] Adding view %lld for output %s (%ux%u @%.2fx)\n",
          (long long)flutter_view_id, out.name, out.width(), out.height(),
          pixel_ratio);
  if (FlutterEngineAddView(view->engine, &add_info) != kSuccess) {
    fprintf(stderr, "[DrmView] FlutterEngineAddView(%lld) rejected\n",
            (long long)flutter_view_id);
    std::lock_guard<std::mutex> lock(view->view_map_mutex);
    view->view_to_output.erase(flutter_view_id);
    return 0;
  }
  view->multi_view = true;
  return 1;
}

void fl_drm_view_set_outputs_changed_callback(
    FlDrmView* view,
    FlDrmOutputsChangedCallback callback,
    void* user_data) {
  if (!view) {
    return;
  }
  view->outputs_changed_user_data = user_data;
  view->outputs_changed_callback = callback;
}

void fl_drm_view_set_output_layout(FlDrmView* view,
                                    uint32_t index,
                                    double logical_x,
                                    double logical_y,
                                    double scale) {
  if (!view || index >= view->display.num_outputs() || scale <= 0) {
    return;
  }
  if (view->placements.size() < view->display.num_outputs()) {
    view->placements.resize(view->display.num_outputs());
  }
  view->placements[index].placed = true;
  view->placements[index].logical_x = logical_x;
  view->placements[index].logical_y = logical_y;
  view->placements[index].scale = scale;
  fprintf(stderr, "[DrmView] output %s placed at (%.0f,%.0f) @%.2fx\n",
          view->display.output(index).name, logical_x, logical_y, scale);
  RebuildInputRegions(view);
}

// Platform thread entry — runs epoll loop for libinput, engine tasks, external fds.
static void* PlatformThreadEntry(void* arg) {
  auto* view = static_cast<FlDrmView*>(arg);

  // Install VT switching signal handlers (no SA_RESTART so blocked reads
  // return EINTR — needed to break out of the swap chain's flip wait).
  if (view && view->vt_initialized) {
    struct sigaction sa = {};
    sa.sa_handler = VtReleaseHandler;
    sigaction(SIGUSR2, &sa, nullptr);
    sa.sa_handler = VtAcquireHandler;
    sigaction(SIGRTMIN, &sa, nullptr);
  }

  if (!view) {
    return nullptr;
  }

  // Record this as the platform thread for RunsOnCurrentThread().
  view->task_runner.SetPlatformThread();

  view->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (view->epoll_fd < 0) {
    fprintf(stderr, "[DrmView] epoll_create1 failed\n");
    return nullptr;
  }

  struct epoll_event ev = {};
  ev.events = EPOLLIN;

  // DRM fd: page-flip events are read HERE (single reader, platform thread)
  // via DrainDrmEvents. The raster thread's Present() waits on a condvar
  // signaled by the flip handler instead of reading the fd itself — this is
  // what lets flip completion drive Wayland frame pacing even when the
  // raster thread is idle.
  int drm_fd = view->display.fd();
  if (drm_fd >= 0) {
    ev.data.fd = drm_fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, drm_fd, &ev);
  }

  int input_fd = view->input.fd();
  if (input_fd >= 0) {
    ev.data.fd = input_fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, input_fd, &ev);
  }

  // Seat manager events (libseat mode): session enable/disable, brokered
  // device grants. -1 in direct mode.
  int seat_fd = view->seat.event_fd();
  if (seat_fd >= 0) {
    ev.data.fd = seat_fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, seat_fd, &ev);
  }

  int timer_fd = view->task_runner.timer_fd();
  if (timer_fd >= 0) {
    ev.data.fd = timer_fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, timer_fd, &ev);
  }

  int hotplug_fd = view->hotplug_monitor
                       ? udev_monitor_get_fd(view->hotplug_monitor)
                       : -1;
  if (hotplug_fd >= 0) {
    ev.data.fd = hotplug_fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, hotplug_fd, &ev);
  }

  for (int i = 0; i < view->external_fd_count; i++) {
    ev.data.fd = view->external_fds[i].fd;
    epoll_ctl(view->epoll_fd, EPOLL_CTL_ADD, view->external_fds[i].fd, &ev);
  }

  fprintf(stderr, "[DrmView] Platform thread entering epoll loop\n");

  while (view->running) {
    // Handle VT switch events from signal handlers.
    if (g_vt_pending_release) {
      g_vt_pending_release = 0;
      // The chord that triggered the switch (Ctrl+Alt+Fn) is still held;
      // its release events will go to the other VT. Release everything now
      // so no layer is left with stuck modifiers.
      view->input.ReleaseAllKeys();
      fprintf(stderr, "[VT] Switched away from VT%d\n", view->vt_num);
    }
    // Complete output teardowns whose views the engine has released
    // (queued by the RemoveView callback on an engine thread).
    {
      std::vector<std::pair<size_t, int64_t>> teardowns;
      {
        std::lock_guard<std::mutex> lock(view->teardown_mutex);
        teardowns.swap(view->pending_teardowns);
      }
      for (auto& teardown : teardowns) {
        CompleteOutputTeardown(view, teardown.first, teardown.second);
      }
    }

    // Session pause/resume from the seat manager (libseat mode). Same
    // recovery paths as the VT signal flags below, plus the input-fd
    // round-trip: on disable logind/seatd revoke every device fd after the
    // ack; on enable libinput reopens them through the seat.
    if (view->seat.TakePendingDisable()) {
      view->vt_active = false;
      view->input.ReleaseAllKeys();
      view->input.Suspend();
      view->seat.AckDisable();
      fprintf(stderr, "[Seat] Session disabled\n");
    }
    if (view->seat.TakePendingEnable()) {
      view->input.Resume();
      for (auto& res : view->output_resources) {
        if (res.chain) {
          res.chain->RestoreModeset();
        }
      }
      view->cursor.Restore();
      view->vt_active = true;
      view->damage_history_count = 0;
      view->input.ReleaseAllKeys();
      if (view->engine) {
        FlutterEngineScheduleFrame(view->engine);
      }
      fprintf(stderr, "[Seat] Session enabled\n");
    }

    if (g_vt_pending_acquire) {
      g_vt_pending_acquire = 0;
      // Restore every output's CRTC modeset, cursor, and force full repaint.
      for (auto& res : view->output_resources) {
        if (res.chain) {
          res.chain->RestoreModeset();
        }
      }
      view->cursor.Restore();
      view->vt_active = true;
      view->damage_history_count = 0;
      // Any releases that happened while the VT was away are lost — make
      // sure nothing is still considered held.
      view->input.ReleaseAllKeys();
      if (view->engine)
        FlutterEngineScheduleFrame(view->engine);
      fprintf(stderr, "[VT] Switched back to VT%d\n", view->vt_num);
    }

    // Screenshot requested via SIGUSR1 — only when VT is active.
    if (g_screenshot_requested && view->engine && view->vt_active) {
      FlutterEngineScheduleFrame(view->engine);
    }

    // Record toggle pending (SIGRTMIN+1) or a recording API request — the
    // flags are consumed in the present callback, so make sure a frame
    // happens. (A frame with no damage still won't composite; the shell's
    // frame pump provides the damage while recording.)
    if ((g_record_toggle ||
         g_api_record_start.load(std::memory_order_relaxed) >= 0 ||
         g_api_record_stop.load(std::memory_order_relaxed)) &&
        view->engine && view->vt_active) {
      FlutterEngineScheduleFrame(view->engine);
    }

    view->task_runner.DrainExpired(view->engine);

    // 2. epoll_wait — 16ms (~60Hz) when active, 100ms when VT inactive.
    struct epoll_event events[8];
    int nfds = epoll_wait(view->epoll_fd, events, 8,
                           view->vt_active ? 16 : 100);

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;

      if (fd == drm_fd) {
        // Page flip landed on some CRTC — demuxes to the owning chain,
        // waking its presenter's flip wait; the primary's completion also
        // fires the present callback (Wayland frame pacing).
        FlDrmSwapChain::DrainDrmEvents(drm_fd);
      } else if (fd == input_fd) {
        if (view->vt_active) {
          // 3. Handle libinput events.
          view->input.ProcessEvents(view->engine);
          // Update the hardware cursor: it follows the pointer onto the
          // CRTC of whichever output contains it.
          uint32_t cursor_crtc = 0;
          int cursor_x = 0, cursor_y = 0;
          if (view->input.CursorPlacement(&cursor_crtc, &cursor_x,
                                          &cursor_y)) {
            view->cursor.MoveTo(cursor_crtc, cursor_x, cursor_y);
          }
        } else {
          // Drain libinput events to keep fd from staying readable.
          view->input.DrainEvents();
        }
      } else if (fd == seat_fd) {
        // Runs the libseat callbacks — pending flags are consumed at the
        // top of the next loop iteration.
        view->seat.Dispatch();
      } else if (fd == timer_fd) {
        view->task_runner.DrainExpired(view->engine);
      } else if (fd == hotplug_fd) {
        // Drain all queued uevents, then rescan once — a plug/unplug often
        // emits several change events.
        bool drm_change = false;
        struct udev_device* dev;
        while ((dev = udev_monitor_receive_device(view->hotplug_monitor))) {
          const char* action = udev_device_get_action(dev);
          if (action && strcmp(action, "change") == 0) {
            drm_change = true;
          }
          udev_device_unref(dev);
        }
        if (drm_change && view->vt_active) {
          HandleDrmHotplug(view);
        }
      } else {
        // 5. External fd — call registered callback (even when VT inactive,
        // to avoid blocking Wayland/X11 client protocols).
        for (int j = 0; j < view->external_fd_count; j++) {
          if (fd == view->external_fds[j].fd) {
            view->external_fds[j].callback(view->external_fds[j].user_data);
            break;
          }
        }
      }
    }
  }

  close(view->epoll_fd);
  view->epoll_fd = -1;
  fprintf(stderr, "[DrmView] Platform thread exited\n");
  return nullptr;
}

// Drain UI tasks that the engine posted to the UI task runner.
static void DrainUITasks(FlDrmView* view) {
  uint64_t now = FlutterEngineGetCurrentTime();
  std::vector<FlDrmView::UITask> expired;
  {
    std::lock_guard<std::mutex> lock(view->ui_mutex);
    auto it = view->ui_tasks.begin();
    while (it != view->ui_tasks.end()) {
      if (it->target_time_nanos <= now) {
        expired.push_back(*it);
        it = view->ui_tasks.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& t : expired) {
    FlutterEngineRunTask(view->engine, &t.task);
  }
}

void fl_drm_view_run(FlDrmView* view) {
  signal(SIGUSR1, ScreenshotSignalHandler);
  signal(SIGRTMIN + 1, RecordSignalHandler);

  if (!view) {
    return;
  }

  view->running = true;
  InitGCDIntegration();

  // Start platform thread (epoll loop for input + engine platform tasks).
  pthread_create(&view->platform_thread, nullptr, PlatformThreadEntry, view);

  // FLUTTER_DRM_SECONDARY_TEST: drive each secondary output with a color
  // cycle from its own thread/context. Exercises the multi-CRTC flip demux
  // end-to-end (each thread's Present() only advances when ITS flips land)
  // without needing per-output Flutter views yet.
  const char* secondary_test = getenv("FLUTTER_DRM_SECONDARY_TEST");
  if (secondary_test && secondary_test[0] && view->multi_view) {
    fprintf(stderr,
            "[DrmView] FLUTTER_DRM_SECONDARY_TEST ignored (multi-view owns "
            "the secondary outputs)\n");
    secondary_test = nullptr;
  }
  if (secondary_test && secondary_test[0]) {
    for (size_t i = 0; i < view->output_resources.size(); i++) {
      if (i == view->display.primary_index()) {
        continue;
      }
      auto& res = view->output_resources[i];
      if (!res.chain) {
        continue;
      }
      // Context created here (main thread) — FlDrmEgl's bookkeeping isn't
      // thread-safe; the context is only made current on the test thread.
      EGLContext ctx = view->egl.CreateAuxContext();
      if (ctx == EGL_NO_CONTEXT) {
        continue;
      }
      const char* name = view->display.output(i).name;
      view->secondary_test_threads.emplace_back([view, &res, ctx, name]() {
        if (!view->egl.MakeCurrent(ctx, res.egl_surface)) {
          fprintf(stderr, "[SecondaryTest %s] MakeCurrent failed\n", name);
          return;
        }
        uint64_t frame = 0;
        while (view->running) {
          if (!view->vt_active) {
            usleep(100 * 1000);
            continue;
          }
          // Slow RGB sweep, ~3s period at 60Hz.
          float t = (frame % 180) / 180.0f;
          glClearColor(t, 1.0f - t, 0.25f, 1.0f);
          glClear(GL_COLOR_BUFFER_BIT);
          if (!res.chain->Present()) {
            break;
          }
          if (frame % 120 == 0) {
            fprintf(stderr, "[SecondaryTest %s] frame %llu\n", name,
                    (unsigned long long)frame);
          }
          frame++;  // Present() blocks on the prior flip → vsync-paced.
        }
        eglMakeCurrent(view->egl.display(), EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
        fprintf(stderr, "[SecondaryTest %s] exited\n", name);
      });
      fprintf(stderr, "[DrmView] Secondary test thread started for %s\n",
              name);
    }
  }

  // Main thread: drain GCD main queue + UI engine tasks.
  // GCD handles @MainActor / async-await continuations.
  // UI engine tasks handle onBeginFrame, widget building, layout, paint.
  int gcd_fd = gcd_get_main_queue_handle ? gcd_get_main_queue_handle() : -1;

  fprintf(stderr, "[DrmView] Main thread entering UI loop (GCD fd=%d)\n", gcd_fd);

  while (view->running) {
    // Drain UI engine tasks.
    DrainUITasks(view);

    // Drain GCD main queue (handles @MainActor, DispatchQueue.main.async, etc.)
    if (gcd_main_queue_drain) {
      gcd_main_queue_drain(nullptr);
    }

    // Compute timeout: time until next UI task deadline, or 100ms if idle.
    int timeout_ms = 100;
    {
      std::lock_guard<std::mutex> lock(view->ui_mutex);
      if (!view->ui_tasks.empty()) {
        uint64_t now = FlutterEngineGetCurrentTime();
        uint64_t earliest = UINT64_MAX;
        for (auto& t : view->ui_tasks) {
          if (t.target_time_nanos < earliest)
            earliest = t.target_time_nanos;
        }
        if (earliest <= now) {
          timeout_ms = 0;
        } else {
          uint64_t delta = (earliest - now) / 1000000;
          timeout_ms = static_cast<int>(delta < 100 ? delta : 100);
        }
      }
    }

    // Poll on the wakeup pipe (signaled when UI tasks are posted) AND the
    // GCD main queue handle, so a DispatchQueue.main.async from another
    // thread wakes this loop instead of waiting out the timeout. The handle
    // is an eventfd the drain callback does NOT reset — it must be read()
    // once signaled, or it stays readable and the poll spins. Reading it
    // BEFORE the next drain means an enqueue after the read re-signals the
    // fd: a wakeup can be spurious but never lost.
    if (view->ui_wakeup_read_fd >= 0 || gcd_fd >= 0) {
      struct pollfd pfds[2] = {
          {view->ui_wakeup_read_fd, POLLIN, 0},  // fd -1: poll ignores it
          {gcd_fd, POLLIN, 0},
      };
      poll(pfds, 2, timeout_ms);
      // Drain the pipe
      if (pfds[0].revents & POLLIN) {
        char buf[64];
        while (read(view->ui_wakeup_read_fd, buf, sizeof(buf)) > 0) {}
      }
      // Clear the GCD eventfd; the drain at the top of the loop does the work.
      if (pfds[1].revents & POLLIN) {
        uint64_t v = 0;
        ssize_t r = read(gcd_fd, &v, sizeof(v));
        (void)r;
      }
    } else {
      if (timeout_ms > 0) usleep(timeout_ms * 1000);
      else usleep(1000);
    }
  }

  // The platform thread has stopped draining DRM events (or is about to),
  // so wake any presenter still blocked on an in-flight flip before joining.
  for (auto& res : view->output_resources) {
    if (res.chain) {
      res.chain->AbortFlipWait();
    }
  }
  for (auto& t : view->secondary_test_threads) {
    t.join();
  }
  view->secondary_test_threads.clear();

  // Wait for platform thread to exit.
  pthread_join(view->platform_thread, nullptr);
  fprintf(stderr, "[DrmView] UI loop exited\n");
}

void fl_drm_view_shutdown(FlDrmView* view) {
  if (view) {
    view->running = false;
  }
}

void fl_drm_view_arm_capture(FlDrmView* view) {
  // Keep capturing for a couple of frames; each GetImage re-arms, so the
  // capture rate tracks the client's poll rate. Schedule a frame so an idle
  // desktop refreshes the mirror at least once after arming.
  // Keep capturing for a few frames after each arm. An idle desktop won't
  // present from ScheduleFrame alone (no damage), so the shell's frame-tick
  // pump watches fl_drm_view_capture_active() and dirties a pixel to force
  // presents while this is non-zero — that's what refreshes the mirror.
  g_x11_cap_frames.store(4, std::memory_order_relaxed);
  if (view && view->engine) {
    FlutterEngineScheduleFrame(view->engine);
  }
}

int fl_drm_view_capture_active(void) {
  return g_x11_cap_frames.load(std::memory_order_relaxed) > 0 ? 1 : 0;
}

int fl_drm_view_read_capture(int x, int y, int w, int h, uint8_t* dst,
                             int dst_len) {
  if (!dst || w <= 0 || h <= 0) {
    return 0;
  }
  if (static_cast<size_t>(w) * h * 4 > static_cast<size_t>(dst_len)) {
    return 0;
  }
  std::lock_guard<std::mutex> lk(g_x11_cap_mu);
  if (!g_x11_cap_valid) {
    return 0;
  }
  const int fw = static_cast<int>(g_x11_cap_w);
  const int fh = static_cast<int>(g_x11_cap_h);
  const uint8_t* src = g_x11_cap_buf.data();
  for (int ty = 0; ty < h; ty++) {
    uint8_t* drow = dst + static_cast<size_t>(ty) * w * 4;
    const int sy = y + ty;
    // glReadPixels is bottom-up; X wants top-down.
    const int srow = fh - 1 - sy;
    if (sy < 0 || sy >= fh) {
      std::memset(drow, 0, static_cast<size_t>(w) * 4);
      continue;
    }
    const uint8_t* scan = src + static_cast<size_t>(srow) * fw * 4;
    for (int tx = 0; tx < w; tx++) {
      uint8_t* d = drow + static_cast<size_t>(tx) * 4;
      const int sx = x + tx;
      if (sx < 0 || sx >= fw) {
        d[0] = d[1] = d[2] = 0;
        d[3] = 0xff;
        continue;
      }
      const uint8_t* s = scan + static_cast<size_t>(sx) * 4;
      d[0] = s[2];  // B
      d[1] = s[1];  // G
      d[2] = s[0];  // R
      d[3] = 0xff;  // X (opaque; depth-32 visual's 4th byte is unused)
    }
  }
  return 1;
}

uint32_t fl_drm_view_get_width(FlDrmView* view) {
  return view ? view->display.width() : 0;
}

uint32_t fl_drm_view_get_height(FlDrmView* view) {
  return view ? view->display.height() : 0;
}

FlDrmFlutterEngine fl_drm_view_get_engine(FlDrmView* view) {
  return view ? view->engine : nullptr;
}

void* fl_drm_view_get_proc_address(const char* name) {
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

void fl_drm_view_set_external_texture_callback(
    FlDrmView* view,
    FlDrmExternalTextureCallback callback,
    void* user_data) {
  if (view) {
    view->external_texture_callback = callback;
    view->external_texture_user_data = user_data;
  }
}

void fl_drm_view_send_metrics(FlDrmView* view, double pixel_ratio) {
  if (!view || !view->engine) return;
  if (pixel_ratio < 0.5) pixel_ratio = 0.5;
  if (pixel_ratio > 4.0) pixel_ratio = 4.0;
  FlutterWindowMetricsEvent metrics = {};
  metrics.struct_size = sizeof(metrics);
  metrics.width = view->display.width();
  metrics.height = view->display.height();
  metrics.pixel_ratio = pixel_ratio;
  metrics.view_id = 0;
  FlutterEngineSendWindowMetricsEvent(view->engine, &metrics);
  // Keep the primary's pointer region in step with the DPI change.
  size_t primary = view->display.primary_index();
  if (primary < view->placements.size() &&
      view->placements[primary].placed) {
    view->placements[primary].scale = pixel_ratio;
    RebuildInputRegions(view);
  }
  fprintf(stderr, "[DrmView] Updated pixel_ratio=%.2f\n", pixel_ratio);
}

void* fl_drm_view_get_egl_display(FlDrmView* view) {
  return view ? view->egl.display() : nullptr;
}

void fl_drm_view_add_external_fd(FlDrmView* view,
                                  int fd,
                                  void (*callback)(void* user_data),
                                  void* user_data) {
  if (!view || fd < 0 || !callback) return;
  if (view->external_fd_count >= FlDrmView::kMaxExternalFds) {
    fprintf(stderr, "[DrmView] Too many external fds (max %d)\n",
            FlDrmView::kMaxExternalFds);
    return;
  }
  auto& ext = view->external_fds[view->external_fd_count++];
  ext.fd = fd;
  ext.callback = callback;
  ext.user_data = user_data;
}

void fl_drm_view_set_cursor_shape(FlDrmView* view, int shape) {
  if (!view) {
    return;
  }
  view->cursor.SetShape(static_cast<flutter::FlCursorShape>(shape));
}

void fl_drm_view_set_present_callback(FlDrmView* view,
                                       FlDrmPresentCallback cb,
                                       void* user_data) {
  if (!view) {
    return;
  }
  view->present_callback_user_data = user_data;
  view->present_callback = cb;
}

void fl_drm_view_set_record_frame_callback(FlDrmView* view,
                                            FlDrmRecordFrameCallback callback,
                                            void* user_data) {
  if (!view) {
    return;
  }
  view->record_frame_user_data = user_data;
  view->record_frame_callback = callback;
}

void fl_drm_view_set_record_dmabuf_callback(FlDrmView* view,
                                            FlDrmRecordDmabufCallback callback,
                                            void* user_data) {
  if (!view) {
    return;
  }
  view->record_dmabuf_user_data = user_data;
  view->record_dmabuf_callback = callback;
}

void fl_drm_view_recording_set_dmabuf(int enable) {
  g_api_record_dmabuf.store(enable != 0, std::memory_order_release);
}

void fl_drm_view_recording_notify_source_changed(void) {
  g_api_record_epoch.fetch_add(1, std::memory_order_release);
}

void fl_drm_view_recording_release_dmabuf_slot(int slot) {
  if (slot < 0 || slot >= kDmabufRing) {
    return;
  }
  g_dmabuf_busy.fetch_and(~(1u << slot), std::memory_order_acq_rel);
}

void fl_drm_view_recording_start(FlDrmView* view, int downscale_shift) {
  fl_drm_view_recording_start_cropped(view, downscale_shift, 0, 0, 0, 0);
}

void fl_drm_view_recording_start_cropped(FlDrmView* view, int downscale_shift,
                                          int x, int y, int w, int h) {
  if (downscale_shift < 0) downscale_shift = 0;
  if (downscale_shift > 3) downscale_shift = 3;
  g_api_record_texture.store(-1, std::memory_order_relaxed);
  fl_drm_view_recording_set_crop(x, y, w, h);
  g_api_record_start.store(downscale_shift, std::memory_order_release);
  if (view && view->engine) {
    FlutterEngineScheduleFrame(view->engine);
  }
}

void fl_drm_view_recording_start_texture(FlDrmView* view, int downscale_shift,
                                          int64_t texture_id,
                                          int w, int h, int content_top_down) {
  if (downscale_shift < 0) downscale_shift = 0;
  if (downscale_shift > 3) downscale_shift = 3;
  g_api_record_tex_topdown.store(content_top_down != 0,
                                 std::memory_order_relaxed);
  g_api_record_texture.store(texture_id, std::memory_order_relaxed);
  // The crop dims freeze the output size (the source blit scales to fit);
  // origin is meaningless for a texture source.
  fl_drm_view_recording_set_crop(0, 0, w, h);
  g_api_record_start.store(downscale_shift, std::memory_order_release);
  if (view && view->engine) {
    FlutterEngineScheduleFrame(view->engine);
  }
}

void fl_drm_view_recording_set_crop(int x, int y, int w, int h) {
  g_api_crop[0].store(x, std::memory_order_relaxed);
  g_api_crop[1].store(y, std::memory_order_relaxed);
  g_api_crop[2].store(w, std::memory_order_relaxed);
  g_api_crop[3].store(h, std::memory_order_relaxed);
}

void fl_drm_view_recording_stop(FlDrmView* view) {
  g_api_record_stop.store(true, std::memory_order_release);
  if (view && view->engine) {
    FlutterEngineScheduleFrame(view->engine);
  }
}

int fl_drm_view_recording_active(void) {
  return g_api_recording.load(std::memory_order_acquire) ? 1 : 0;
}

uint32_t fl_drm_view_get_refresh_mhz(FlDrmView* view) {
  if (!view) {
    return 0;
  }
  return view->display.mode().vrefresh * 1000;
}

void fl_drm_view_destroy(FlDrmView* view) {
  if (!view) {
    return;
  }

  if (view->engine) {
    FlutterEngineShutdown(view->engine);
    view->engine = nullptr;
  }

  // Restore VT to text mode before restoring CRTC.
  if (view->vt_initialized && view->vt_tty_fd >= 0) {
    ioctl(view->vt_tty_fd, KDSKBMODE,
          view->vt_saved_kb_mode >= 0 ? view->vt_saved_kb_mode : K_UNICODE);
    ioctl(view->vt_tty_fd, KDSETMODE, KD_TEXT);
    struct vt_mode vtm = {};
    vtm.mode = VT_AUTO;
    ioctl(view->vt_tty_fd, VT_SETMODE, &vtm);
    g_vt_tty_fd = -1;
    g_vt_drm_fd = -1;
    g_vt_active_ptr = nullptr;
    close(view->vt_tty_fd);
    view->vt_tty_fd = -1;
  }

  view->display.RestoreCrtc();

  // ~FlDrmView closes the seat last (declared first) — the display fd is
  // released through it.
  g_seat_ptr = nullptr;

  if (view->hotplug_monitor) {
    udev_monitor_unref(view->hotplug_monitor);
    view->hotplug_monitor = nullptr;
  }
  if (view->hotplug_udev) {
    udev_unref(view->hotplug_udev);
    view->hotplug_udev = nullptr;
  }

  // Swap chains are deleted by ~FlDrmView (before EGL/GBM surfaces).
  delete view;
  fprintf(stderr, "[DrmView] Destroyed\n");
}

}  // extern "C"
