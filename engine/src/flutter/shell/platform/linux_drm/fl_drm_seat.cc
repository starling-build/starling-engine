// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_seat.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace flutter {

// libseat's API, declared locally: the engine's build sysroot (Debian
// bullseye) has no libseat-dev, so the library is dlopen'd and these must
// match libseat.h (stable since 0.6).
extern "C" {
struct libseat_seat_listener {
  void (*enable_seat)(struct libseat* seat, void* userdata);
  void (*disable_seat)(struct libseat* seat, void* userdata);
};
}

namespace {

struct LibseatApi {
  void* handle = nullptr;
  struct libseat* (*open_seat)(const libseat_seat_listener*, void*) = nullptr;
  int (*disable_seat)(struct libseat*) = nullptr;
  int (*close_seat)(struct libseat*) = nullptr;
  const char* (*seat_name)(struct libseat*) = nullptr;
  int (*open_device)(struct libseat*, const char*, int*) = nullptr;
  int (*close_device)(struct libseat*, int) = nullptr;
  int (*get_fd)(struct libseat*) = nullptr;
  int (*dispatch)(struct libseat*, int) = nullptr;
  int (*switch_session)(struct libseat*, int) = nullptr;
};

LibseatApi g_api;

bool LoadLibseat() {
  if (g_api.handle) {
    return true;
  }
  void* h = dlopen("libseat.so.1", RTLD_NOW | RTLD_LOCAL);
  if (!h) {
    return false;
  }
#define FL_SEAT_SYM(field, name)                                          \
  g_api.field = reinterpret_cast<decltype(g_api.field)>(dlsym(h, name)); \
  if (!g_api.field) {                                                     \
    fprintf(stderr, "[Seat] libseat.so.1 missing symbol %s\n", name);     \
    dlclose(h);                                                           \
    return false;                                                         \
  }
  FL_SEAT_SYM(open_seat, "libseat_open_seat")
  FL_SEAT_SYM(disable_seat, "libseat_disable_seat")
  FL_SEAT_SYM(close_seat, "libseat_close_seat")
  FL_SEAT_SYM(seat_name, "libseat_seat_name")
  FL_SEAT_SYM(open_device, "libseat_open_device")
  FL_SEAT_SYM(close_device, "libseat_close_device")
  FL_SEAT_SYM(get_fd, "libseat_get_fd")
  FL_SEAT_SYM(dispatch, "libseat_dispatch")
  FL_SEAT_SYM(switch_session, "libseat_switch_session")
#undef FL_SEAT_SYM
  g_api.handle = h;
  return true;
}

}  // namespace

struct FlDrmSeatCallbacks {
  static void EnableSeat(struct libseat* seat, void* userdata) {
    auto* self = static_cast<FlDrmSeat*>(userdata);
    self->active_ = true;
    self->pending_enable_.store(true);
  }
  static void DisableSeat(struct libseat* seat, void* userdata) {
    auto* self = static_cast<FlDrmSeat*>(userdata);
    self->active_ = false;
    self->pending_disable_.store(true);
  }
};

static const libseat_seat_listener kSeatListener = {
    FlDrmSeatCallbacks::EnableSeat,
    FlDrmSeatCallbacks::DisableSeat,
};

FlDrmSeat::~FlDrmSeat() {
  Close();
}

bool FlDrmSeat::Open() {
  const char* env = getenv("FLUTTER_DRM_SEAT");
  const char* mode = (env && *env) ? env : "auto";

  if (strcmp(mode, "direct") == 0) {
    return true;
  }
  bool forced = strcmp(mode, "libseat") == 0;
  if (!forced && geteuid() == 0) {
    // auto + root: the sudo dev workflow — keep the historical direct path.
    return true;
  }

  if (!LoadLibseat()) {
    if (forced) {
      fprintf(stderr, "[Seat] FLUTTER_DRM_SEAT=libseat but libseat.so.1 "
                      "unavailable\n");
      return false;
    }
    fprintf(stderr, "[Seat] libseat.so.1 not found — direct device access\n");
    return true;
  }

  seat_ = g_api.open_seat(&kSeatListener, this);
  if (!seat_) {
    if (forced) {
      fprintf(stderr, "[Seat] libseat_open_seat failed (no logind session / "
                      "seatd?)\n");
      return false;
    }
    fprintf(stderr, "[Seat] no seat manager — direct device access\n");
    return true;
  }

  // The first enable arrives via dispatch; devices can't be opened before it.
  int guard = 100;
  while (!active_ && guard-- > 0) {
    if (g_api.dispatch(seat_, 1000) < 0) {
      fprintf(stderr, "[Seat] libseat_dispatch failed during startup\n");
      g_api.close_seat(seat_);
      seat_ = nullptr;
      return !forced;
    }
  }
  if (!active_) {
    fprintf(stderr, "[Seat] seat never became active\n");
    g_api.close_seat(seat_);
    seat_ = nullptr;
    return !forced;
  }
  // Startup does a full modeset anyway — the initial enable is not an event.
  pending_enable_.store(false);

  fprintf(stderr, "[Seat] libseat active on %s\n", g_api.seat_name(seat_));
  return true;
}

void FlDrmSeat::Close() {
  if (seat_) {
    g_api.close_seat(seat_);
    seat_ = nullptr;
  }
}

int FlDrmSeat::OpenDevice(const char* path, int flags) {
  if (!seat_) {
    return open(path, flags | O_CLOEXEC);
  }
  int fd = -1;
  int device_id = g_api.open_device(seat_, path, &fd);
  if (device_id < 0 || fd < 0) {
    fprintf(stderr, "[Seat] libseat_open_device(%s) failed\n", path);
    return -1;
  }
  std::lock_guard<std::mutex> lock(devices_mutex_);
  fd_to_device_id_[fd] = device_id;
  return fd;
}

void FlDrmSeat::CloseDevice(int fd) {
  if (fd < 0) {
    return;
  }
  if (seat_) {
    int device_id = -1;
    {
      std::lock_guard<std::mutex> lock(devices_mutex_);
      auto it = fd_to_device_id_.find(fd);
      if (it != fd_to_device_id_.end()) {
        device_id = it->second;
        fd_to_device_id_.erase(it);
      }
    }
    if (device_id >= 0) {
      g_api.close_device(seat_, device_id);
    }
  }
  close(fd);
}

int FlDrmSeat::event_fd() const {
  return seat_ ? g_api.get_fd(seat_) : -1;
}

void FlDrmSeat::Dispatch() {
  if (seat_) {
    g_api.dispatch(seat_, 0);
  }
}

void FlDrmSeat::AckDisable() {
  if (seat_) {
    g_api.disable_seat(seat_);
  }
}

bool FlDrmSeat::SwitchSession(int session) {
  if (!seat_) {
    return false;
  }
  return g_api.switch_session(seat_, session) == 0;
}

const char* FlDrmSeat::seat_name() const {
  if (seat_) {
    return g_api.seat_name(seat_);
  }
  const char* env = getenv("XDG_SEAT");
  return (env && *env) ? env : "seat0";
}

}  // namespace flutter
