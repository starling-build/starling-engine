// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_INPUT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_INPUT_H_

#include <libinput.h>
#include <set>
#include <xkbcommon/xkbcommon.h>

#include <mutex>
#include <vector>

#include "embedder.h"

namespace flutter {

class FlDrmSeat;

// One placed output in the virtual pointer space: a rect in global logical
// ("virtual desktop") coordinates, the Flutter view rendering it, and the
// CRTC carrying its hardware cursor plane.
struct FlInputRegion {
  double logical_x = 0;
  double logical_y = 0;
  double logical_w = 0;
  double logical_h = 0;
  double scale = 1.0;   // physical pixels per logical unit on this output
  int64_t view_id = 0;  // Flutter view receiving this region's events
  uint32_t crtc_id = 0;
  // >=0: externally sourced output — events go to the external router
  // (view-local physical coords), never to a Flutter view.
  int external_output = -1;

  bool Contains(double x, double y) const {
    return x >= logical_x && x < logical_x + logical_w && y >= logical_y &&
           y < logical_y + logical_h;
  }
};

class FlDrmInput {
 public:
  FlDrmInput();
  ~FlDrmInput();

  // Router for regions whose output is externally sourced. phase is the
  // FlutterPointerPhase raw value; x/y are view-local PHYSICAL.
  using ExternalRouter = void (*)(int output, int phase, double x, double y,
                                  int64_t buttons, double scroll_dx,
                                  double scroll_dy, void* user_data);
  void set_external_router(ExternalRouter router, void* user_data) {
    external_router_ = router;
    external_router_user_ = user_data;
  }

  // Initialize libinput + xkbcommon. The display size seeds a single default
  // region (scale 1) until SetRegions is called. Device opens are brokered
  // through the seat (plain open() in direct mode).
  bool Initialize(uint32_t display_width, uint32_t display_height,
                  FlDrmSeat* seat);

  // Session pause/resume (libseat mode). On disable the seat manager revokes
  // every input fd (reads turn ENODEV, devices drop out); Resume() makes
  // libinput re-enumerate and reopen them through the seat.
  void Suspend();
  void Resume();

  // Replace the virtual-desktop layout. The pointer moves within the union
  // of the regions and crosses between adjacent ones; each event carries the
  // owning region's view id and view-local PHYSICAL coordinates. While any
  // button is held, events stay routed to the region where the press began
  // (with coordinates extending beyond its bounds) — pointer capture, which
  // is what lets a window drag cross a seam. Absolute devices map onto the
  // view-0 (primary) region.
  void SetRegions(const std::vector<FlInputRegion>& regions);

  // Get the libinput fd for epoll.
  int fd() const;

  // Process pending libinput events. Sends pointer/key events to engine.
  void ProcessEvents(FlutterEngine engine);

  // Drain pending libinput events without sending to Flutter.
  // Call when VT is inactive to keep the fd from staying readable.
  void DrainEvents();

  // Hardware-cursor placement: the CRTC of the output CONTAINING the pointer
  // (not the capture region) and output-local physical coordinates.
  bool CursorPlacement(uint32_t* crtc_id, int* x, int* y) const;

 private:
  ExternalRouter external_router_ = nullptr;
  void* external_router_user_ = nullptr;
  void HandlePointerMotion(libinput_event* event);
  void HandlePointerMotionAbsolute(libinput_event* event);
  void HandlePointerButton(libinput_event* event);
  void HandlePointerAxis(libinput_event* event);
  void HandleKeyboard(libinput_event* event);

  void SendPointerEvent(FlutterPointerPhase phase,
                        double scroll_delta_x, double scroll_delta_y,
                        int64_t buttons);

  // Index of the region containing (x, y), or -1.
  int RegionContaining(double x, double y) const;
  // Index of the view-0 region, falling back to 0.
  size_t PrimaryRegion() const;
  // Region events route to: the capture region while buttons are held,
  // otherwise the region containing the pointer.
  size_t RoutingRegion() const;

  static uint64_t EvdevToHID(uint32_t evdev_code);

  libinput* li_ = nullptr;
  FlDrmSeat* seat_ = nullptr;
  struct xkb_context* xkb_ctx_ = nullptr;
 public:
  // Release every held key: synthesize key-up events into the engine and
  // reset xkb. Key releases are lost across a VT switch (they go to the
  // other console), so held modifiers would otherwise stay stuck at every
  // layer — xkb poisons each later character ('s' -> Ctrl+S) and app-side
  // modifier tracking wedges on Ctrl/Alt held forever.
  void ReleaseAllKeys();

 private:
  std::set<uint32_t> pressed_keys_;
  struct xkb_keymap* xkb_keymap_ = nullptr;
  struct xkb_state* xkb_state_ = nullptr;

  FlutterEngine engine_ = nullptr;

  // Virtual-desktop pointer state (global LOGICAL coordinates). Guarded by
  // state_mutex_: event handlers run on the platform thread, SetRegions on
  // the main thread.
  mutable std::mutex state_mutex_;
  std::vector<FlInputRegion> regions_;
  double vx_ = 0;
  double vy_ = 0;
  size_t current_region_ = 0;  // region containing the pointer
  size_t capture_region_ = 0;  // region owning events while buttons held
  bool has_capture_ = false;

  bool pointer_added_ = false;
  bool pointer_down_ = false;
  int64_t buttons_ = 0;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_INPUT_H_
