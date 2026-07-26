// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_swap_chain.h"

#include <GLES2/gl2.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <chrono>

#include "fl_drm_egl.h"

namespace flutter {

FlDrmSwapChain::FlDrmSwapChain(FlDrmDisplay* display,
                               FlDrmEgl* egl,
                               const FlDrmOutput& output,
                               gbm_surface* surface,
                               EGLSurface egl_surface)
    : display_(display),
      egl_(egl),
      output_(output),
      surface_(surface),
      egl_surface_(egl_surface) {}

FlDrmSwapChain::~FlDrmSwapChain() {
  if (previous_bo_) {
    drmModeRmFB(display_->fd(), previous_fb_);
    gbm_surface_release_buffer(surface_, previous_bo_);
    previous_bo_ = nullptr;
  }
  if (current_bo_) {
    drmModeRmFB(display_->fd(), current_fb_);
    gbm_surface_release_buffer(surface_, current_bo_);
    current_bo_ = nullptr;
  }
}

bool FlDrmSwapChain::InitialModeSet(EGLContext context) {
  // Clear to black and swap to get the first buffer.
  if (context != EGL_NO_CONTEXT) {
    egl_->MakeCurrent(context, egl_surface_);
  } else {
    egl_->MakeCurrent(egl_surface_);
  }
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  egl_->SwapBuffers(egl_surface_);

  current_bo_ = gbm_surface_lock_front_buffer(surface_);
  if (!current_bo_) {
    fprintf(stderr, "[SwapChain %s] Initial lock_front_buffer failed\n",
            output_.name);
    return false;
  }

  uint32_t handle = gbm_bo_get_handle(current_bo_).u32;
  uint32_t stride = gbm_bo_get_stride(current_bo_);
  uint32_t width = gbm_bo_get_width(current_bo_);
  uint32_t height = gbm_bo_get_height(current_bo_);

  int ret = drmModeAddFB(display_->fd(), width, height, 24, 32,
                          stride, handle, &current_fb_);
  if (ret) {
    fprintf(stderr, "[SwapChain %s] drmModeAddFB failed: %d\n", output_.name,
            ret);
    return false;
  }

  uint32_t conn_id = output_.connector_id;
  ret = drmModeSetCrtc(display_->fd(), output_.crtc_id, current_fb_,
                        0, 0, &conn_id, 1, &output_.mode);
  if (ret) {
    if (errno == EACCES || errno == EPERM) {
      // Not DRM master — defer modeset to VT acquire (RestoreModeset).
      // current_bo_ and current_fb_ are valid; modeset will happen on acquire.
      fprintf(stderr, "[SwapChain %s] Deferring modeset (not DRM master)\n",
              output_.name);
      egl_->ClearCurrent();
      return true;
    }
    fprintf(stderr, "[SwapChain %s] drmModeSetCrtc failed: %d\n", output_.name,
            ret);
    return false;
  }

  // Clear current context so the presenting thread can claim it.
  egl_->ClearCurrent();

  mode_set_done_ = true;
  fprintf(stderr, "[SwapChain %s] Initial mode set done (crtc=%u %ux%u)\n",
          output_.name, output_.crtc_id, width, height);
  return true;
}

// v3 flip handler: the kernel reports which CRTC completed; user_data is the
// chain that queued the flip (passed to drmModePageFlip), so events from all
// outputs on the shared fd demux without any global lookup.
static void PageFlipHandler2(int /*fd*/, unsigned int /*sequence*/,
                              unsigned int tv_sec, unsigned int tv_usec,
                              unsigned int crtc_id, void* user_data) {
  auto* swap_chain = static_cast<FlDrmSwapChain*>(user_data);
  if (crtc_id && crtc_id != swap_chain->crtc_id()) {
    // user_data is authoritative; a mismatch would mean flips were queued
    // with the wrong cookie. Loud because it should never happen.
    fprintf(stderr, "[SwapChain %s] flip demux mismatch: event crtc=%u\n",
            swap_chain->name(), crtc_id);
  }
  uint64_t flip_ns = static_cast<uint64_t>(tv_sec) * 1000000000ull +
                     static_cast<uint64_t>(tv_usec) * 1000ull;
  swap_chain->HandlePageFlipEvent(flip_ns);
}

void FlDrmSwapChain::DrainDrmEvents(int drm_fd) {
  // Platform thread only — the single reader of the DRM fd. drmHandleEvent
  // is only called when epoll reported the fd readable, so it won't block.
  // One call dispatches every pending event, each to its own chain.
  drmEventContext ev = {};
  ev.version = 3;  // page_flip_handler2 (carries crtc_id)
  ev.page_flip_handler2 = PageFlipHandler2;
  drmHandleEvent(drm_fd, &ev);
}

bool FlDrmSwapChain::Present() {
  // [STARLING-DEBUG] frame accounting (env-gated to keep production quiet;
  // the dev launcher always exports the var, so empty means off).
  static const bool s_dbg = [] {
    const char* env = getenv("STARLING_DEBUG_CAPTURE");
    return env && env[0];
  }();
  static int s_present_n = 0;
  int fn = s_present_n++;
  if (s_dbg)
    fprintf(stderr, "[SwapChain %s] Present#%d enter (waiting_for_flip=%d)\n",
            output_.name, fn, (int)waiting_for_flip_);
  // Wait for any pending page flip to complete. The flip event is read on
  // the platform thread (DrainDrmEvents), which signals the condvar. The
  // periodic timeout re-checks VT state — after a VT switch the event may
  // never arrive because DRM master was dropped.
  bool waited;
  {
    std::unique_lock<std::mutex> lock(flip_mutex_);
    waited = waiting_for_flip_;
    int waited_ms = 0;
    while (waiting_for_flip_) {
      if (vt_active_ && !*vt_active_) {
        waiting_for_flip_ = false;
        break;
      }
      flip_cv_.wait_for(lock, std::chrono::milliseconds(50));
      waited_ms += 50;
      if (waiting_for_flip_ && waited_ms >= 2000) {
        // A flip event never arrived (lost event, driver hiccup). Better a
        // logged recovery + possible flicker than a permanently dead
        // desktop.
        fprintf(stderr,
                "[SwapChain %s] flip wait watchdog: no flip event after "
                "%d ms - recovering\n",
                output_.name, waited_ms);
        waiting_for_flip_ = false;
        break;
      }
    }
  }
  if (waited && s_dbg) {
    fprintf(stderr, "[SwapChain %s] Present#%d flip-wait cleared\n",
            output_.name, fn);
  }

  // The last queued flip has landed (or been abandoned): the buffer it
  // displaced is finally off the screen and safe to destroy. Doing this
  // before the flip completes — as this code once did — RmFBs the
  // framebuffer the display is still scanning out, which force-disables
  // the plane and flickers the screen on every continuously-animated frame.
  if (previous_bo_) {
    drmModeRmFB(display_->fd(), previous_fb_);
    gbm_surface_release_buffer(surface_, previous_bo_);
    previous_bo_ = nullptr;
    previous_fb_ = 0;
  }

  // If VT is not active, swap EGL buffers to keep GL state consistent
  // but skip the DRM page flip (we don't have DRM master).
  if (vt_active_ && !*vt_active_) {
    egl_->SwapBuffers(egl_surface_);
    gbm_bo* bo = gbm_surface_lock_front_buffer(surface_);
    if (bo) gbm_surface_release_buffer(surface_, bo);
    return true;
  }

  egl_->SwapBuffers(egl_surface_);

  gbm_bo* next_bo = gbm_surface_lock_front_buffer(surface_);
  if (!next_bo) {
    fprintf(stderr, "[SwapChain %s] lock_front_buffer failed\n", output_.name);
    return false;
  }

  uint32_t handle = gbm_bo_get_handle(next_bo).u32;
  uint32_t stride = gbm_bo_get_stride(next_bo);
  uint32_t width = gbm_bo_get_width(next_bo);
  uint32_t height = gbm_bo_get_height(next_bo);

  uint32_t next_fb = 0;
  int ret = drmModeAddFB(display_->fd(), width, height, 24, 32,
                          stride, handle, &next_fb);
  if (ret) {
    fprintf(stderr, "[SwapChain %s] drmModeAddFB failed: %d\n", output_.name,
            ret);
    gbm_surface_release_buffer(surface_, next_bo);
    return false;
  }

  // Raise the flag BEFORE queueing the flip: the flip event can land on the
  // platform thread within the ioctl-return window (virtio flips complete
  // near-instantly), and an event consumed before the flag is up leaves the
  // next Present waiting for a signal that already fired.
  {
    std::lock_guard<std::mutex> lock(flip_mutex_);
    waiting_for_flip_ = true;
  }
  ret = drmModePageFlip(display_->fd(), output_.crtc_id, next_fb,
                         DRM_MODE_PAGE_FLIP_EVENT, this);
  if (ret) {
    {
      std::lock_guard<std::mutex> lock(flip_mutex_);
      waiting_for_flip_ = false;
    }
    // DRM master lost (VT switch) — clean up silently.
    if (errno == EACCES || errno == EPERM) {
      drmModeRmFB(display_->fd(), next_fb);
      gbm_surface_release_buffer(surface_, next_bo);
      return true;
    }
    fprintf(stderr, "[SwapChain %s] drmModePageFlip failed: %d\n",
            output_.name, ret);
    drmModeRmFB(display_->fd(), next_fb);
    gbm_surface_release_buffer(surface_, next_bo);
    return false;
  }

  // The displaced buffer stays alive until the flip we just queued lands
  // (released at the top of the next Present, after the flip-wait).
  previous_bo_ = current_bo_;
  previous_fb_ = current_fb_;

  if (s_dbg)
    fprintf(stderr,
            "[SwapChain %s] Present#%d flip queued fb=%u %ux%u handle=%u stride=%u\n",
            output_.name, fn, next_fb, width, height, handle, stride);
  current_bo_ = next_bo;
  current_fb_ = next_fb;

  return true;
}

bool FlDrmSwapChain::RestoreModeset() {
  if (!current_fb_) return false;
  uint32_t conn_id = output_.connector_id;
  int ret = drmModeSetCrtc(display_->fd(), output_.crtc_id, current_fb_,
                            0, 0, &conn_id, 1, &output_.mode);
  if (ret) {
    fprintf(stderr, "[SwapChain %s] RestoreModeset failed: %d\n", output_.name,
            ret);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(flip_mutex_);
    waiting_for_flip_ = false;
  }
  flip_cv_.notify_all();
  fprintf(stderr, "[SwapChain %s] Modeset restored\n", output_.name);
  return true;
}

void FlDrmSwapChain::AbortFlipWait() {
  {
    std::lock_guard<std::mutex> lock(flip_mutex_);
    waiting_for_flip_ = false;
  }
  flip_cv_.notify_all();
}

void FlDrmSwapChain::HandlePageFlipEvent(uint64_t flip_time_ns) {
  {
    std::lock_guard<std::mutex> lock(flip_mutex_);
    waiting_for_flip_ = false;
  }
  flip_cv_.notify_all();
  // Fire the present callback (platform thread) with the kernel's scanout
  // timestamp so the compositor can pace Wayland clients off real vsync.
  if (present_cb_) {
    present_cb_(present_cb_user_data_, flip_time_ns);
  }
}

}  // namespace flutter
