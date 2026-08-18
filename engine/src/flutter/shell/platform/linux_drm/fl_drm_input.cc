// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_input.h"
#include "fl_drm_seat.h"

#include <fcntl.h>
#include <libudev.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace flutter {

// libinput interface callbacks — brokered through the seat so libseat mode
// gets revocable fds from logind/seatd (user_data is the FlDrmSeat).
static int OpenRestricted(const char* path, int flags, void* user_data) {
  auto* seat = static_cast<FlDrmSeat*>(user_data);
  int fd = seat ? seat->OpenDevice(path, flags)
                : open(path, flags | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "[Input] Failed to open %s\n", path);
  }
  return fd;
}

static void CloseRestricted(int fd, void* user_data) {
  auto* seat = static_cast<FlDrmSeat*>(user_data);
  if (seat) {
    seat->CloseDevice(fd);
  } else {
    close(fd);
  }
}

static const struct libinput_interface kLibinputInterface = {
    .open_restricted = OpenRestricted,
    .close_restricted = CloseRestricted,
};

FlDrmInput::FlDrmInput() = default;

FlDrmInput::~FlDrmInput() {
  if (xkb_state_) {
    xkb_state_unref(xkb_state_);
  }
  if (xkb_keymap_) {
    xkb_keymap_unref(xkb_keymap_);
  }
  if (xkb_ctx_) {
    xkb_context_unref(xkb_ctx_);
  }
  if (li_) {
    libinput_unref(li_);
  }
}

bool FlDrmInput::Initialize(uint32_t display_width, uint32_t display_height,
                            FlDrmSeat* seat) {
  seat_ = seat;
  // Single default region at scale 1 until SetRegions installs the real
  // layout — event coordinates equal physical primary pixels either way.
  FlInputRegion region;
  region.logical_w = display_width;
  region.logical_h = display_height;
  regions_ = {region};
  vx_ = display_width / 2.0;
  vy_ = display_height / 2.0;

  // Set up libinput with udev.
  struct udev* udev = udev_new();
  if (!udev) {
    fprintf(stderr, "[Input] udev_new failed\n");
    return false;
  }

  li_ = libinput_udev_create_context(&kLibinputInterface, seat_, udev);
  udev_unref(udev);

  if (!li_) {
    fprintf(stderr, "[Input] libinput_udev_create_context failed\n");
    return false;
  }

  const char* seat_name = seat_ ? seat_->seat_name() : "seat0";
  if (libinput_udev_assign_seat(li_, seat_name) != 0) {
    fprintf(stderr, "[Input] libinput_udev_assign_seat(%s) failed\n",
            seat_name);
    return false;
  }

  // Set up xkbcommon for keyboard.
  xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_ctx_) {
    fprintf(stderr, "[Input] xkb_context_new failed\n");
    return false;
  }

  xkb_keymap_ = xkb_keymap_new_from_names(xkb_ctx_, nullptr,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
  if (!xkb_keymap_) {
    fprintf(stderr, "[Input] xkb_keymap_new_from_names failed\n");
    return false;
  }

  xkb_state_ = xkb_state_new(xkb_keymap_);
  if (!xkb_state_) {
    fprintf(stderr, "[Input] xkb_state_new failed\n");
    return false;
  }

  fprintf(stderr, "[Input] libinput + xkbcommon initialized\n");
  return true;
}

int FlDrmInput::fd() const {
  return li_ ? libinput_get_fd(li_) : -1;
}

void FlDrmInput::Suspend() {
  if (li_) {
    libinput_suspend(li_);
  }
}

void FlDrmInput::Resume() {
  if (li_ && libinput_resume(li_) != 0) {
    fprintf(stderr, "[Input] libinput_resume failed\n");
  }
}

void FlDrmInput::DrainEvents() {
  if (!li_) return;
  libinput_dispatch(li_);
  struct libinput_event* event;
  while ((event = libinput_get_event(li_)) != nullptr) {
    libinput_event_destroy(event);
  }
}

void FlDrmInput::ProcessEvents(FlutterEngine engine) {
  engine_ = engine;

  libinput_dispatch(li_);
  struct libinput_event* event;
  while ((event = libinput_get_event(li_)) != nullptr) {
    enum libinput_event_type type = libinput_event_get_type(event);
    switch (type) {
      case LIBINPUT_EVENT_POINTER_MOTION:
        HandlePointerMotion(event);
        break;
      case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
        HandlePointerMotionAbsolute(event);
        break;
      case LIBINPUT_EVENT_POINTER_BUTTON:
        HandlePointerButton(event);
        break;
      case LIBINPUT_EVENT_POINTER_AXIS:
        HandlePointerAxis(event);
        break;
      case LIBINPUT_EVENT_KEYBOARD_KEY:
        HandleKeyboard(event);
        break;
      default:
        break;
    }
    libinput_event_destroy(event);
  }
}

void FlDrmInput::SetRegions(const std::vector<FlInputRegion>& regions) {
  if (regions.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(state_mutex_);
  // Regions are identified by view id across rebuilds — indexes shift when
  // an output is removed (hotplug).
  int64_t capture_view = has_capture_ && capture_region_ < regions_.size()
                             ? regions_[capture_region_].view_id
                             : -1;
  regions_ = regions;
  // Keep the pointer where it is when still on some output; otherwise
  // re-center it on the primary (its output may have been unplugged).
  int idx = RegionContaining(vx_, vy_);
  if (idx < 0) {
    const FlInputRegion& p = regions_[PrimaryRegion()];
    vx_ = p.logical_x + p.logical_w / 2.0;
    vy_ = p.logical_y + p.logical_h / 2.0;
    idx = (int)PrimaryRegion();
  }
  current_region_ = (size_t)idx;
  if (has_capture_) {
    has_capture_ = false;
    for (size_t i = 0; i < regions_.size(); i++) {
      if (regions_[i].view_id == capture_view) {
        capture_region_ = i;
        has_capture_ = true;
        break;
      }
    }
    // Capture view gone (its output unplugged mid-drag): the gesture
    // continues against whatever contains the pointer now.
  }
  if (capture_region_ >= regions_.size()) {
    capture_region_ = current_region_;
  }
  fprintf(stderr, "[Input] %zu pointer region(s) installed\n",
          regions_.size());
}

int FlDrmInput::RegionContaining(double x, double y) const {
  for (size_t i = 0; i < regions_.size(); i++) {
    if (regions_[i].Contains(x, y)) {
      return (int)i;
    }
  }
  return -1;
}

size_t FlDrmInput::PrimaryRegion() const {
  for (size_t i = 0; i < regions_.size(); i++) {
    if (regions_[i].view_id == 0) {
      return i;
    }
  }
  return 0;
}

size_t FlDrmInput::RoutingRegion() const {
  return has_capture_ ? capture_region_ : current_region_;
}

bool FlDrmInput::CursorPlacement(uint32_t* crtc_id, int* x, int* y) const {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (regions_.empty()) {
    return false;
  }
  const FlInputRegion& r = regions_[current_region_];
  if (crtc_id) {
    *crtc_id = r.crtc_id;
  }
  if (x) {
    *x = (int)((vx_ - r.logical_x) * r.scale);
  }
  if (y) {
    *y = (int)((vy_ - r.logical_y) * r.scale);
  }
  return true;
}

void FlDrmInput::HandlePointerMotion(libinput_event* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto* pe = libinput_event_get_pointer_event(event);
  double dx = libinput_event_pointer_get_dx(pe);
  double dy = libinput_event_pointer_get_dy(pe);

  // Deltas are device/physical-ish units: divide by the current output's
  // scale so pointer speed is constant in physical pixels across outputs.
  const FlInputRegion& cur = regions_[current_region_];
  double nx = vx_ + dx / cur.scale;
  double ny = vy_ + dy / cur.scale;

  // Move within the union of the output rects: prefer the full motion, then
  // axis slides along an edge, else stay clamped inside the current output
  // (an L-shaped layout's inner corner behaves like a wall).
  int target = RegionContaining(nx, ny);
  if (target < 0) {
    if ((target = RegionContaining(nx, vy_)) >= 0) {
      ny = vy_;
    } else if ((target = RegionContaining(vx_, ny)) >= 0) {
      nx = vx_;
    } else {
      double eps = 1.0 / cur.scale;
      nx = nx < cur.logical_x ? cur.logical_x
           : (nx > cur.logical_x + cur.logical_w - eps
                  ? cur.logical_x + cur.logical_w - eps
                  : nx);
      ny = ny < cur.logical_y ? cur.logical_y
           : (ny > cur.logical_y + cur.logical_h - eps
                  ? cur.logical_y + cur.logical_h - eps
                  : ny);
      target = (int)current_region_;
    }
  }
  vx_ = nx;
  vy_ = ny;
  current_region_ = (size_t)target;

  FlutterPointerPhase phase;
  if (pointer_down_) {
    phase = kMove;
  } else {
    phase = pointer_added_ ? kHover : kAdd;
  }

  SendPointerEvent(phase, 0, 0, buttons_);
  if (!pointer_added_) {
    pointer_added_ = true;
  }
}

void FlDrmInput::HandlePointerMotionAbsolute(libinput_event* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto* pe = libinput_event_get_pointer_event(event);
  // Absolute devices (tablets, the shell-drive uinput harness) map onto the
  // primary output's physical space, as before multi-output.
  const FlInputRegion& p = regions_[PrimaryRegion()];
  double px = libinput_event_pointer_get_absolute_x_transformed(
      pe, (uint32_t)(p.logical_w * p.scale));
  double py = libinput_event_pointer_get_absolute_y_transformed(
      pe, (uint32_t)(p.logical_h * p.scale));
  vx_ = p.logical_x + px / p.scale;
  vy_ = p.logical_y + py / p.scale;
  current_region_ = PrimaryRegion();

  FlutterPointerPhase phase;
  if (pointer_down_) {
    phase = kMove;
  } else {
    phase = pointer_added_ ? kHover : kAdd;
  }

  SendPointerEvent(phase, 0, 0, buttons_);
  if (!pointer_added_) {
    pointer_added_ = true;
  }
}

void FlDrmInput::InjectPointerAbs(FlutterEngine engine, double x, double y,
                                  int64_t buttons, double wheel_dx,
                                  double wheel_dy) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (regions_.empty()) {
    return;
  }
  // engine_ is normally set by ProcessEvents; an injected event can be the
  // first pointer event this process ever sees.
  engine_ = engine;

  // Same transform as HandlePointerMotionAbsolute: the caller's pixels are
  // the primary output's physical space, so scale and virtual-desktop
  // placement stay correct without the caller knowing either.
  const FlInputRegion& p = regions_[PrimaryRegion()];
  vx_ = p.logical_x + x / p.scale;
  vy_ = p.logical_y + y / p.scale;
  current_region_ = PrimaryRegion();

  if (!pointer_added_) {
    SendPointerEvent(kAdd, 0, 0, 0);
    pointer_added_ = true;
  }

  // Buttons arrive as absolute state; the transitions are ours to find.
  // Press before release, so a chord that swaps buttons in one report still
  // reads as a continuous gesture rather than an up/down pair.
  const int64_t pressed = buttons & ~buttons_;
  const int64_t released = buttons_ & ~buttons;
  buttons_ = buttons;

  if (pressed) {
    if (!pointer_down_) {
      // Pointer capture, exactly as HandlePointerButton establishes it.
      capture_region_ = current_region_;
      has_capture_ = true;
    }
    pointer_down_ = true;
    SendPointerEvent(kDown, 0, 0, buttons_);
  } else if (released) {
    pointer_down_ = (buttons_ != 0);
    SendPointerEvent(pointer_down_ ? kMove : kUp, 0, 0, buttons_);
    if (!pointer_down_) {
      has_capture_ = false;
    }
  } else {
    SendPointerEvent(pointer_down_ ? kMove : kHover, 0, 0, buttons_);
  }

  // Scroll rides its own event, like HandlePointerAxis — a pointer event
  // carrying both a phase change and a scroll signal is not a shape the
  // framework expects.
  if (wheel_dx != 0 || wheel_dy != 0) {
    SendPointerEvent(pointer_down_ ? kMove : kHover, wheel_dx, wheel_dy,
                     buttons_);
  }
}

void FlDrmInput::HandlePointerButton(libinput_event* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto* pe = libinput_event_get_pointer_event(event);
  uint32_t button = libinput_event_pointer_get_button(pe);
  auto state = libinput_event_pointer_get_button_state(pe);

  // Map Linux button codes to Flutter button flags.
  int64_t flutter_button = 0;
  switch (button) {
    case BTN_LEFT:
      flutter_button = kFlutterPointerButtonMousePrimary;
      break;
    case BTN_RIGHT:
      flutter_button = kFlutterPointerButtonMouseSecondary;
      break;
    case BTN_MIDDLE:
      flutter_button = kFlutterPointerButtonMouseMiddle;
      break;
    default:
      break;
  }

  if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
    buttons_ |= flutter_button;
  } else {
    buttons_ &= ~flutter_button;
  }

  FlutterPointerPhase phase;
  if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
    if (!pointer_down_) {
      // Pointer capture: the view under the first press owns every event
      // until the last button is released, with coordinates extending past
      // its bounds — a drag that crosses a seam stays one gesture.
      capture_region_ = current_region_;
      has_capture_ = true;
    }
    pointer_down_ = true;
    phase = kDown;
  } else {
    pointer_down_ = (buttons_ != 0);
    phase = pointer_down_ ? kMove : kUp;
  }

  if (!pointer_added_) {
    SendPointerEvent(kAdd, 0, 0, 0);
    pointer_added_ = true;
  }

  SendPointerEvent(phase, 0, 0, buttons_);

  if (!pointer_down_) {
    has_capture_ = false;
  }
}

void FlDrmInput::HandlePointerAxis(libinput_event* event) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  auto* pe = libinput_event_get_pointer_event(event);
  double dx = 0, dy = 0;

  if (libinput_event_pointer_has_axis(
          pe, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
    dx = libinput_event_pointer_get_axis_value(
        pe, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
  }
  if (libinput_event_pointer_has_axis(
          pe, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
    dy = libinput_event_pointer_get_axis_value(
        pe, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
  }

  // Scale to Flutter's expected scroll delta (pixels).
  // libinput gives scroll in "scroll units", multiply by a factor.
  dx *= 20.0;
  dy *= 20.0;

  if (!pointer_added_) {
    SendPointerEvent(kAdd, 0, 0, 0);
    pointer_added_ = true;
  }

  SendPointerEvent(kHover, dx, dy, buttons_);
}

extern "C" void fl_drm_request_vt_switch(int vt);

void FlDrmInput::ReleaseAllKeys() {
  // Send key-ups for everything still held so the engine, the shell, and
  // every child app see a clean release before the state reset.
  for (uint32_t key : pressed_keys_) {
    xkb_keysym_t keysym = xkb_state_
        ? xkb_state_key_get_one_sym(xkb_state_, key + 8)
        : XKB_KEY_NoSymbol;
    FlutterKeyEvent key_event = {};
    key_event.struct_size = sizeof(key_event);
    key_event.timestamp = static_cast<double>(
        FlutterEngineGetCurrentTime() / 1000);
    key_event.type = kFlutterKeyEventTypeUp;
    key_event.physical = EvdevToHID(key);
    key_event.logical = keysym;
    key_event.character = nullptr;
    key_event.synthesized = true;
    if (engine_) {
      FlutterEngineSendKeyEvent(engine_, &key_event, nullptr, nullptr);
    }
  }
  pressed_keys_.clear();
  if (xkb_keymap_) {
    struct xkb_state* fresh = xkb_state_new(xkb_keymap_);
    if (fresh) {
      if (xkb_state_) {
        xkb_state_unref(xkb_state_);
      }
      xkb_state_ = fresh;
    }
  }
}

void FlDrmInput::HandleKeyboard(libinput_event* event) {
  auto* ke = libinput_event_get_keyboard_event(event);
  uint32_t key = libinput_event_keyboard_get_key(ke);
  auto state = libinput_event_keyboard_get_key_state(ke);

  // Update xkb state.
  xkb_state_update_key(xkb_state_, key + 8,
                        state == LIBINPUT_KEY_STATE_PRESSED
                            ? XKB_KEY_DOWN
                            : XKB_KEY_UP);
  if (state == LIBINPUT_KEY_STATE_PRESSED) {
    pressed_keys_.insert(key);
  } else {
    pressed_keys_.erase(key);
  }

  // Get keysym and UTF-8 character.
  xkb_keysym_t keysym = xkb_state_key_get_one_sym(xkb_state_, key + 8);

  // Ctrl+Alt+Fn: VT switch. The VT keyboard runs K_OFF (the console keymap
  // must not see our keys), so the kernel won't switch by itself anymore —
  // do it here. xkb maps the chord to XF86Switch_VT_n on standard keymaps;
  // match plain Fn + modifiers as a fallback.
  if (state == LIBINPUT_KEY_STATE_PRESSED) {
    int vt = 0;
    if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
      vt = static_cast<int>(keysym - XKB_KEY_XF86Switch_VT_1) + 1;
    } else if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
      bool ctrl = xkb_state_mod_name_is_active(
                      xkb_state_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
      bool alt = xkb_state_mod_name_is_active(
                     xkb_state_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
      if (ctrl && alt) vt = static_cast<int>(keysym - XKB_KEY_F1) + 1;
    }
    if (vt > 0) {
      fl_drm_request_vt_switch(vt);
      return;
    }
  }
  char utf8[8] = {};
  xkb_state_key_get_utf8(xkb_state_, key + 8, utf8, sizeof(utf8));

  FlutterKeyEvent key_event = {};
  key_event.struct_size = sizeof(key_event);
  key_event.timestamp = static_cast<double>(
      FlutterEngineGetCurrentTime() / 1000);  // microseconds
  key_event.type = (state == LIBINPUT_KEY_STATE_PRESSED) ? kFlutterKeyEventTypeDown
                                                          : kFlutterKeyEventTypeUp;
  key_event.physical = EvdevToHID(key);
  key_event.logical = keysym;
  key_event.character = utf8[0] ? utf8 : nullptr;
  key_event.synthesized = false;

  FlutterEngineSendKeyEvent(engine_, &key_event, nullptr, nullptr);
}

void FlDrmInput::SendPointerEvent(FlutterPointerPhase phase,
                                   double scroll_delta_x,
                                   double scroll_delta_y,
                                   int64_t buttons) {
  // Route to the capture region (buttons held) or the containing region,
  // in that region's view-local PHYSICAL coordinates.
  const FlInputRegion& r = regions_[RoutingRegion()];
  double x = (vx_ - r.logical_x) * r.scale;
  double y = (vy_ - r.logical_y) * r.scale;

  FlutterPointerEvent event = {};
  event.struct_size = sizeof(event);
  event.phase = phase;
  event.x = x;
  event.y = y;
  event.scroll_delta_x = scroll_delta_x;
  event.scroll_delta_y = scroll_delta_y;
  event.buttons = buttons;
  event.device = 0;
  event.device_kind = kFlutterPointerDeviceKindMouse;
  event.timestamp = FlutterEngineGetCurrentTime() / 1000;  // microseconds
  event.view_id = r.view_id;

  if (scroll_delta_x != 0 || scroll_delta_y != 0) {
    event.signal_kind = kFlutterPointerSignalKindScroll;
  }

  FlutterEngineResult result = FlutterEngineSendPointerEvent(engine_, &event, 1);

  // Debug: log pointer events
  static int event_count = 0;
  if (++event_count <= 20 || phase == kDown || phase == kUp) {
    const char* phase_name = "?";
    switch (phase) {
      case kAdd: phase_name = "add"; break;
      case kRemove: phase_name = "remove"; break;
      case kHover: phase_name = "hover"; break;
      case kDown: phase_name = "DOWN"; break;
      case kUp: phase_name = "UP"; break;
      case kMove: phase_name = "move"; break;
      default: break;
    }
    fprintf(stderr,
            "[Input] #%d %s view=%lld (%.0f,%.0f) buttons=%lld result=%d\n",
            event_count, phase_name, (long long)r.view_id, x, y,
            (long long)buttons, (int)result);
  }
}

uint64_t FlDrmInput::EvdevToHID(uint32_t evdev_code) {
  // Map evdev key codes to USB HID usage codes (page 0x07). Letter rows are
  // NOT contiguous in evdev (16-25 qwertyuiop, 30-38 asdfghjkl, 44-50
  // zxcvbnm) — the old range arithmetic mis-mapped most letters and
  // swallowed Left Shift (42) into a bogus "J-O" range. Keep this the exact
  // inverse of WaylandIntegration.hidToEvdev (DesktopShellApp).
  switch (evdev_code) {
    case 1:   return 0x29;  // Escape
    case 2:   return 0x1E;  // 1
    case 3:   return 0x1F;  // 2
    case 4:   return 0x20;  // 3
    case 5:   return 0x21;  // 4
    case 6:   return 0x22;  // 5
    case 7:   return 0x23;  // 6
    case 8:   return 0x24;  // 7
    case 9:   return 0x25;  // 8
    case 10:  return 0x26;  // 9
    case 11:  return 0x27;  // 0
    case 12:  return 0x2D;  // -
    case 13:  return 0x2E;  // =
    case 14:  return 0x2A;  // Backspace
    case 15:  return 0x2B;  // Tab
    case 16:  return 0x14;  // Q
    case 17:  return 0x1A;  // W
    case 18:  return 0x08;  // E
    case 19:  return 0x15;  // R
    case 20:  return 0x17;  // T
    case 21:  return 0x1C;  // Y
    case 22:  return 0x18;  // U
    case 23:  return 0x0C;  // I
    case 24:  return 0x12;  // O
    case 25:  return 0x13;  // P
    case 26:  return 0x2F;  // [
    case 27:  return 0x30;  // ]
    case 28:  return 0x28;  // Enter
    case 29:  return 0xE0;  // Left Control
    case 30:  return 0x04;  // A
    case 31:  return 0x16;  // S
    case 32:  return 0x07;  // D
    case 33:  return 0x09;  // F
    case 34:  return 0x0A;  // G
    case 35:  return 0x0B;  // H
    case 36:  return 0x0D;  // J
    case 37:  return 0x0E;  // K
    case 38:  return 0x0F;  // L
    case 39:  return 0x33;  // ;
    case 40:  return 0x34;  // '
    case 41:  return 0x35;  // `
    case 42:  return 0xE1;  // Left Shift
    case 43:  return 0x31;  // backslash
    case 44:  return 0x1D;  // Z
    case 45:  return 0x1B;  // X
    case 46:  return 0x06;  // C
    case 47:  return 0x19;  // V
    case 48:  return 0x05;  // B
    case 49:  return 0x11;  // N
    case 50:  return 0x10;  // M
    case 51:  return 0x36;  // ,
    case 52:  return 0x37;  // .
    case 53:  return 0x38;  // /
    case 54:  return 0xE5;  // Right Shift
    case 55:  return 0x55;  // KP *
    case 56:  return 0xE2;  // Left Alt
    case 57:  return 0x2C;  // Space
    case 58:  return 0x39;  // CapsLock
    case 59:  return 0x3A;  // F1
    case 60:  return 0x3B;  // F2
    case 61:  return 0x3C;  // F3
    case 62:  return 0x3D;  // F4
    case 63:  return 0x3E;  // F5
    case 64:  return 0x3F;  // F6
    case 65:  return 0x40;  // F7
    case 66:  return 0x41;  // F8
    case 67:  return 0x42;  // F9
    case 68:  return 0x43;  // F10
    case 69:  return 0x53;  // NumLock
    case 70:  return 0x47;  // ScrollLock
    case 71:  return 0x5F;  // KP 7
    case 72:  return 0x60;  // KP 8
    case 73:  return 0x61;  // KP 9
    case 74:  return 0x56;  // KP -
    case 75:  return 0x5C;  // KP 4
    case 76:  return 0x5D;  // KP 5
    case 77:  return 0x5E;  // KP 6
    case 78:  return 0x57;  // KP +
    case 79:  return 0x59;  // KP 1
    case 80:  return 0x5A;  // KP 2
    case 81:  return 0x5B;  // KP 3
    case 82:  return 0x62;  // KP 0
    case 83:  return 0x63;  // KP .
    case 87:  return 0x44;  // F11
    case 88:  return 0x45;  // F12
    case 96:  return 0x58;  // KP Enter
    case 97:  return 0xE4;  // Right Control
    case 98:  return 0x54;  // KP /
    case 99:  return 0x46;  // PrintScreen
    case 100: return 0xE6;  // Right Alt
    case 102: return 0x4A;  // Home
    case 103: return 0x52;  // Up
    case 104: return 0x4B;  // PageUp
    case 105: return 0x50;  // Left
    case 106: return 0x4F;  // Right
    case 107: return 0x4D;  // End
    case 108: return 0x51;  // Down
    case 109: return 0x4E;  // PageDown
    case 110: return 0x49;  // Insert
    case 111: return 0x4C;  // Delete
    case 119: return 0x48;  // Pause
    case 125: return 0xE3;  // Left Meta
    case 126: return 0xE7;  // Right Meta
    case 127: return 0x65;  // Menu/Compose
    default:  return static_cast<uint64_t>(evdev_code) | 0x00100000000ULL;
  }
}

}  // namespace flutter
