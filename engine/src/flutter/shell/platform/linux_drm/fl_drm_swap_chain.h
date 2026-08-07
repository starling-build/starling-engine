// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SWAP_CHAIN_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SWAP_CHAIN_H_

#include <EGL/egl.h>
#include <gbm.h>
#include <stdint.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "fl_drm_display.h"

namespace flutter {

class FlDrmEgl;

// Drives ONE output: presents a GBM/EGL surface pair to that output's CRTC
// via page flips. Each connected output gets its own instance; all instances
// share the DRM fd, whose flip events are demuxed by DrainDrmEvents().
class FlDrmSwapChain {
 public:
  // Invoked (on the platform thread) when a page flip completes, with the
  // kernel's scanout timestamp and the flipping CRTC — one callback serves
  // every chain, demuxed by crtc. Wired up to the compositor's frame pacing.
  using PresentCallback = void (*)(void* user_data,
                                   uint64_t flip_time_ns,
                                   uint32_t crtc_id);

  FlDrmSwapChain(FlDrmDisplay* display,
                 FlDrmEgl* egl,
                 const FlDrmOutput& output,
                 gbm_surface* surface,
                 EGLSurface egl_surface);
  ~FlDrmSwapChain();

  // Perform this output's initial mode set with a black frame. With
  // EGL_NO_CONTEXT the shared render context is used (startup, before the
  // engine's raster thread claims it); at hotplug time pass an aux context —
  // the render context is current on another thread by then.
  bool InitialModeSet(EGLContext context = EGL_NO_CONTEXT);

  // Present: eglSwapBuffers → gbm lock → drmModeAddFB → drmModePageFlip.
  // Called with this chain's EGL surface current on the calling thread (the
  // engine's raster thread for the primary; a secondary output's own thread
  // otherwise). Waits for the previously queued flip via condvar — the DRM
  // fd is read exclusively on the platform thread (DrainDrmEvents).
  bool Present();

  // Read pending DRM events (page flips) for ALL swap chains sharing `drm_fd`.
  // MUST be called on the platform thread only, when the fd polls readable.
  // Uses the v3 event context (page_flip_handler2, which carries the CRTC id);
  // each completion is routed to the chain that queued it, waking that chain's
  // Present() wait and firing its present callback with the kernel scanout
  // timestamp.
  static void DrainDrmEvents(int drm_fd);

  // Handle DRM page flip event (platform thread, via DrainDrmEvents).
  void HandlePageFlipEvent(uint64_t flip_time_ns);

  // Restore CRTC modeset after VT switch back (re-displays current_fb_).
  bool RestoreModeset();

  // Wake a Present() blocked on a flip that will never be drained (used at
  // shutdown, after the platform thread has stopped reading the DRM fd).
  void AbortFlipWait();

  // Set pointer to VT active flag for cross-thread VT state checking.
  // The swap chain reads this from the raster thread to skip page flips
  // when the VT is inactive.
  void set_vt_active(volatile bool* p) { vt_active_ = p; }

  void set_present_callback(PresentCallback cb, void* user_data) {
    present_cb_ = cb;
    present_cb_user_data_ = user_data;
  }

  // Mailbox mode: a Present() arriving while the previous flip is still
  // pending DROPS the frame (no swap, no flip, no wait) instead of stalling
  // the calling thread. On for secondary outputs — the raster thread
  // presents every view sequentially, and one 30Hz panel's flip-wait must
  // not throttle a 90Hz one. The primary keeps blocking: its back-pressure
  // is what paces the whole pipeline.
  void set_skip_when_busy(bool skip) { skip_when_busy_ = skip; }

  // Whether frames were dropped since the last flip landed (cleared by the
  // read). The flip bridge forwards this so the app can re-present the
  // final frame of an animation — a drop leaves the panel one frame stale
  // otherwise, with nothing left in flight to fix it.
  bool take_dropped() {
    return dropped_since_flip_.exchange(false, std::memory_order_acq_rel);
  }

  bool waiting_for_flip() const { return waiting_for_flip_; }
  uint32_t crtc_id() const { return output_.crtc_id; }
  const char* name() const { return output_.name; }

 private:
  FlDrmDisplay* display_;
  FlDrmEgl* egl_;
  FlDrmOutput output_;      // this output's connector/CRTC/mode (by value)
  gbm_surface* surface_;    // this output's scanout surface (owned by FlDrmGbm)
  EGLSurface egl_surface_;  // this output's window surface (owned by FlDrmEgl)

  gbm_bo* current_bo_ = nullptr;
  uint32_t current_fb_ = 0;
  // The buffer displaced by the queued flip — still being scanned out until
  // that flip lands, so it is only released after the next flip-wait.
  gbm_bo* previous_bo_ = nullptr;
  uint32_t previous_fb_ = 0;
  bool mode_set_done_ = false;
  volatile bool* vt_active_ = nullptr;

  // Flip synchronization: waiting_for_flip_ is set on the presenting thread
  // when a flip is queued and cleared on the platform thread when it lands.
  std::mutex flip_mutex_;
  std::condition_variable flip_cv_;
  bool waiting_for_flip_ = false;

  // Mailbox mode (secondary outputs). dropped_since_flip_ is written on the
  // presenting thread, consumed on the platform thread by the flip bridge.
  bool skip_when_busy_ = false;
  std::atomic<bool> dropped_since_flip_{false};

  PresentCallback present_cb_ = nullptr;
  void* present_cb_user_data_ = nullptr;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SWAP_CHAIN_H_
