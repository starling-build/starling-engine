// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_egl.h"

#include <stdio.h>
#include <string.h>
#include <vector>

#ifndef EGL_PLATFORM_GBM_MESA
#define EGL_PLATFORM_GBM_MESA 0x31D7
#endif

namespace flutter {

FlDrmEgl::FlDrmEgl() = default;

FlDrmEgl::~FlDrmEgl() {
  if (display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    for (EGLSurface surface : surfaces_) {
      eglDestroySurface(display_, surface);
    }
    surfaces_.clear();
    for (EGLContext context : aux_contexts_) {
      eglDestroyContext(display_, context);
    }
    aux_contexts_.clear();
    if (context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, context_);
    }
    if (resource_context_ != EGL_NO_CONTEXT) {
      eglDestroyContext(display_, resource_context_);
    }
    eglTerminate(display_);
  }
}

bool FlDrmEgl::Initialize(gbm_device* gbm_dev) {
  // Try eglGetPlatformDisplay first, fall back to eglGetDisplay.
  auto eglGetPlatformDisplayEXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (eglGetPlatformDisplayEXT) {
    display_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA,
                                         gbm_dev, nullptr);
  }
  if (display_ == EGL_NO_DISPLAY) {
    display_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbm_dev));
  }
  if (display_ == EGL_NO_DISPLAY) {
    fprintf(stderr, "[EGL] eglGetDisplay failed\n");
    return false;
  }

  EGLint major, minor;
  if (!eglInitialize(display_, &major, &minor)) {
    fprintf(stderr, "[EGL] eglInitialize failed\n");
    return false;
  }
  fprintf(stderr, "[EGL] Initialized: %d.%d\n", major, minor);

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    fprintf(stderr, "[EGL] eglBindAPI(ES) failed\n");
    return false;
  }

  // Choose config — request minimum sizes, then pick the best match
  // for the GBM surface format (XRGB8888). On AMD, eglChooseConfig may
  // return 10-10-10-2 configs first which don't match the GBM surface.
  // clang-format off
  const EGLint config_attribs[] = {
      EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_RED_SIZE,        1,
      EGL_GREEN_SIZE,      1,
      EGL_BLUE_SIZE,       1,
      EGL_STENCIL_SIZE,    8,
      EGL_NONE,
  };
  // clang-format on

  EGLint num_configs = 0;
  eglChooseConfig(display_, config_attribs, nullptr, 0, &num_configs);
  if (num_configs == 0) {
    fprintf(stderr, "[EGL] eglChooseConfig: no configs available\n");
    return false;
  }

  std::vector<EGLConfig> configs(num_configs);
  eglChooseConfig(display_, config_attribs, configs.data(), num_configs,
                  &num_configs);

  // Prefer 8-8-8 config (matches GBM_FORMAT_XRGB8888). Fall back to first.
  config_ = configs[0];
  for (int i = 0; i < num_configs; i++) {
    EGLint r, g, b, a;
    eglGetConfigAttrib(display_, configs[i], EGL_RED_SIZE, &r);
    eglGetConfigAttrib(display_, configs[i], EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(display_, configs[i], EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(display_, configs[i], EGL_ALPHA_SIZE, &a);
    if (r == 8 && g == 8 && b == 8) {
      config_ = configs[i];
      fprintf(stderr, "[EGL] Selected config %d: R%dG%dB%dA%d\n", i, r, g, b, a);
      break;
    }
  }
  {
    EGLint r, g, b, a, st;
    eglGetConfigAttrib(display_, config_, EGL_RED_SIZE, &r);
    eglGetConfigAttrib(display_, config_, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(display_, config_, EGL_BLUE_SIZE, &b);
    eglGetConfigAttrib(display_, config_, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(display_, config_, EGL_STENCIL_SIZE, &st);
    fprintf(stderr, "[EGL] Using config: R%dG%dB%dA%d stencil=%d\n",
            r, g, b, a, st);
  }

  // Create main context.
  const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE,
  };
  context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT,
                              context_attribs);
  if (context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "[EGL] Failed to create main context\n");
    return false;
  }

  // Create resource context (shared with main context).
  resource_context_ = eglCreateContext(display_, config_, context_,
                                       context_attribs);
  if (resource_context_ == EGL_NO_CONTEXT) {
    fprintf(stderr, "[EGL] Failed to create resource context\n");
    return false;
  }

  fprintf(stderr, "[EGL] Contexts created\n");
  return true;
}

EGLSurface FlDrmEgl::CreateWindowSurface(gbm_surface* gbm_surf) {
  EGLSurface surface = eglCreateWindowSurface(
      display_, config_, reinterpret_cast<EGLNativeWindowType>(gbm_surf),
      nullptr);
  if (surface == EGL_NO_SURFACE) {
    fprintf(stderr, "[EGL] eglCreateWindowSurface failed\n");
    return EGL_NO_SURFACE;
  }
  surfaces_.push_back(surface);
  return surface;
}

void FlDrmEgl::DestroyWindowSurface(EGLSurface surface) {
  if (surface == EGL_NO_SURFACE) {
    return;
  }
  for (size_t i = 0; i < surfaces_.size(); i++) {
    if (surfaces_[i] == surface) {
      surfaces_.erase(surfaces_.begin() + i);
      eglDestroySurface(display_, surface);
      return;
    }
  }
}

EGLContext FlDrmEgl::CreateAuxContext() {
  const EGLint context_attribs[] = {
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE,
  };
  EGLContext context =
      eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attribs);
  if (context == EGL_NO_CONTEXT) {
    fprintf(stderr, "[EGL] Failed to create aux context\n");
    return EGL_NO_CONTEXT;
  }
  aux_contexts_.push_back(context);
  return context;
}

bool FlDrmEgl::MakeCurrent() {
  return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
}

bool FlDrmEgl::MakeCurrent(EGLSurface surface) {
  return eglMakeCurrent(display_, surface, surface, context_) == EGL_TRUE;
}

bool FlDrmEgl::MakeCurrent(EGLContext context, EGLSurface surface) {
  return eglMakeCurrent(display_, surface, surface, context) == EGL_TRUE;
}

bool FlDrmEgl::ClearCurrent() {
  return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT) == EGL_TRUE;
}

bool FlDrmEgl::MakeResourceCurrent() {
  return eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        resource_context_) == EGL_TRUE;
}

bool FlDrmEgl::SwapBuffers() {
  return eglSwapBuffers(display_, surface_) == EGL_TRUE;
}

bool FlDrmEgl::SwapBuffers(EGLSurface surface) {
  return eglSwapBuffers(display_, surface) == EGL_TRUE;
}

}  // namespace flutter
