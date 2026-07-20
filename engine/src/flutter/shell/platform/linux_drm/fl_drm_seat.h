// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SEAT_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SEAT_H_

#include <atomic>
#include <map>
#include <mutex>

struct libseat;

namespace flutter {

// Seat/session access for the DRM embedder. Two modes:
//
//  - direct:  open() device nodes directly and manage the VT by hand — needs
//             root (or video+input groups). The historical path; still what
//             run-shell-gpu.sh uses.
//  - libseat: devices come from a seat manager (systemd-logind on a stock
//             distro session, seatd on the Starling image), which also owns
//             VT switching. Unprivileged. libseat.so.1 is dlopen'd — the
//             engine sysroot has no libseat headers or stubs.
//
// FLUTTER_DRM_SEAT selects: "direct", "libseat", or "auto" (default: root →
// direct so the sudo dev workflow is unchanged; non-root → libseat with a
// direct fallback for video/input-group setups).
class FlDrmSeat {
 public:
  FlDrmSeat() = default;
  ~FlDrmSeat();

  // Resolve the mode and, for libseat, open the seat and wait for the first
  // enable. Never fails hard in auto mode — falls back to direct. Returns
  // false only when a forced libseat mode can't be satisfied.
  bool Open();
  void Close();

  bool via_libseat() const { return seat_ != nullptr; }

  // Open/close a device node. Direct mode: plain open()/close(). libseat
  // mode: brokered through the seat manager (which grants DRM master and
  // revocable input fds).
  int OpenDevice(const char* path, int flags);
  void CloseDevice(int fd);

  // libseat event-loop integration (both -1/no-op in direct mode).
  int event_fd() const;
  void Dispatch();

  // Enable/disable events, latched by the libseat callbacks and consumed by
  // the platform thread's loop (mirrors the g_vt_pending_* signal flags).
  bool TakePendingEnable() { return pending_enable_.exchange(false); }
  bool TakePendingDisable() { return pending_disable_.exchange(false); }

  // Acknowledge a disable AFTER rendering has quiesced — the seat manager
  // revokes DRM master / input fds once we ack.
  void AckDisable();

  // Ctrl+Alt+Fn: ask the seat manager to switch sessions (libseat mode only;
  // direct mode keeps the VT_ACTIVATE path in fl_drm_view.cc).
  bool SwitchSession(int session);

  // Seat name for libinput's udev backend ("seat0" in direct mode).
  const char* seat_name() const;

 private:
  struct libseat* seat_ = nullptr;
  std::atomic<bool> pending_enable_{false};
  std::atomic<bool> pending_disable_{false};
  bool active_ = false;  // only touched during Open()'s startup dispatch

  // libseat_close_device takes the device id from open_device, not the fd.
  std::mutex devices_mutex_;
  std::map<int, int> fd_to_device_id_;

  friend struct FlDrmSeatCallbacks;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_SEAT_H_
