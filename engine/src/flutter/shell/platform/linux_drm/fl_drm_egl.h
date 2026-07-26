// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_EGL_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_EGL_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>

#include <vector>

namespace flutter {

// Owns the EGL display, config, and contexts, plus one window surface per
// output. The engine's render context targets the primary surface via the
// argument-less MakeCurrent()/SwapBuffers(); other outputs pass their surface
// explicitly.
class FlDrmEgl {
 public:
  FlDrmEgl();
  ~FlDrmEgl();

  // Initialize EGL display, config, and the render + resource contexts.
  bool Initialize(gbm_device* gbm_device);

  // Create a window surface for one output's GBM surface. Owned by this
  // class; returns EGL_NO_SURFACE on failure.
  EGLSurface CreateWindowSurface(gbm_surface* gbm_surface);

  // Destroy one window surface early (hotplug-removed output). Must not be
  // current on any thread.
  void DestroyWindowSurface(EGLSurface surface);

  // Create an extra context (not shared with the render context) for driving
  // a secondary output from its own thread. Owned by this class.
  EGLContext CreateAuxContext();

  // The surface the engine renders to (view 0).
  void set_primary_surface(EGLSurface surface) { surface_ = surface; }

  bool MakeCurrent();                      // render context + primary surface
  bool MakeCurrent(EGLSurface surface);    // render context + given surface
  bool MakeCurrent(EGLContext context, EGLSurface surface);
  bool ClearCurrent();
  bool MakeResourceCurrent();
  bool SwapBuffers();                      // primary surface
  bool SwapBuffers(EGLSurface surface);

  EGLDisplay display() const { return display_; }

 private:
  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLConfig config_ = nullptr;
  EGLContext context_ = EGL_NO_CONTEXT;
  EGLContext resource_context_ = EGL_NO_CONTEXT;
  EGLSurface surface_ = EGL_NO_SURFACE;  // primary (alias into surfaces_)
  std::vector<EGLSurface> surfaces_;     // all window surfaces, owned
  std::vector<EGLContext> aux_contexts_;  // secondary-output contexts, owned
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_EGL_H_
