// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_display.h"
#include "fl_drm_seat.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace flutter {

namespace {

// Human-readable connector name, e.g. "HDMI-A-1", "DP-2".
void ConnectorName(drmModeConnector* c, char* out, size_t out_size) {
  const char* type_name = "Unknown";
  switch (c->connector_type) {
    case DRM_MODE_CONNECTOR_VGA: type_name = "VGA"; break;
    case DRM_MODE_CONNECTOR_DVII: type_name = "DVI-I"; break;
    case DRM_MODE_CONNECTOR_DVID: type_name = "DVI-D"; break;
    case DRM_MODE_CONNECTOR_DVIA: type_name = "DVI-A"; break;
    case DRM_MODE_CONNECTOR_HDMIA: type_name = "HDMI-A"; break;
    case DRM_MODE_CONNECTOR_HDMIB: type_name = "HDMI-B"; break;
    case DRM_MODE_CONNECTOR_DisplayPort: type_name = "DP"; break;
    case DRM_MODE_CONNECTOR_eDP: type_name = "eDP"; break;
    case DRM_MODE_CONNECTOR_VIRTUAL: type_name = "Virtual"; break;
    default: break;
  }
  snprintf(out, out_size, "%s-%u", type_name, c->connector_type_id);
}

// Selects a mode for a connector: FLUTTER_DRM_MODE ("WxH" or "max") when it
// applies, otherwise the largest available mode. Returns false if the connector
// has no modes.
bool SelectMode(drmModeConnector* connector,
                const char* mode_request,
                drmModeModeInfo* out) {
  if (connector->count_modes == 0) {
    return false;
  }

  if (mode_request) {
    unsigned req_w = 0, req_h = 0;
    if (sscanf(mode_request, "%ux%u", &req_w, &req_h) == 2) {
      for (int i = 0; i < connector->count_modes; i++) {
        if (connector->modes[i].hdisplay == req_w &&
            connector->modes[i].vdisplay == req_h) {
          *out = connector->modes[i];
          return true;
        }
      }
      // Requested mode not on this connector — fall through to largest.
    } else if (strcmp(mode_request, "max") != 0) {
      fprintf(stderr, "[DRM] Invalid FLUTTER_DRM_MODE '%s' (use WxH or max)\n",
              mode_request);
    }
  }

  // Largest mode by pixel count.
  *out = connector->modes[0];
  uint64_t best = 0;
  for (int i = 0; i < connector->count_modes; i++) {
    uint64_t pixels = (uint64_t)connector->modes[i].hdisplay *
                      connector->modes[i].vdisplay;
    if (pixels > best) {
      best = pixels;
      *out = connector->modes[i];
    }
  }
  return true;
}

// Allocates a CRTC for `connector` that is not already used by another output.
// Prefers the connector's currently-attached CRTC when it is free. Marks the
// chosen CRTC in `used_mask`. Returns 0 if none is available.
uint32_t AllocCrtc(int fd,
                   drmModeRes* res,
                   drmModeConnector* connector,
                   uint32_t* used_mask) {
  // Prefer the currently-attached CRTC if it is free and legal for us.
  if (connector->encoder_id) {
    drmModeEncoder* enc = drmModeGetEncoder(fd, connector->encoder_id);
    if (enc) {
      for (int j = 0; j < res->count_crtcs; j++) {
        if (res->crtcs[j] == enc->crtc_id &&
            (enc->possible_crtcs & (1u << j)) &&
            !(*used_mask & (1u << j))) {
          *used_mask |= (1u << j);
          drmModeFreeEncoder(enc);
          return res->crtcs[j];
        }
      }
      drmModeFreeEncoder(enc);
    }
  }

  // Otherwise any free CRTC reachable from one of the connector's encoders.
  for (int e = 0; e < connector->count_encoders; e++) {
    drmModeEncoder* enc = drmModeGetEncoder(fd, connector->encoders[e]);
    if (!enc) {
      continue;
    }
    for (int j = 0; j < res->count_crtcs; j++) {
      if ((enc->possible_crtcs & (1u << j)) && !(*used_mask & (1u << j))) {
        *used_mask |= (1u << j);
        drmModeFreeEncoder(enc);
        return res->crtcs[j];
      }
    }
    drmModeFreeEncoder(enc);
  }
  return 0;
}

}  // namespace

FlDrmDisplay::FlDrmDisplay() = default;

FlDrmDisplay::~FlDrmDisplay() {
  RestoreCrtc();
  for (drmModeCrtc* crtc : saved_crtcs_) {
    if (crtc) {
      drmModeFreeCrtc(crtc);
    }
  }
  saved_crtcs_.clear();
  if (fd_ >= 0) {
    if (seat_) {
      seat_->CloseDevice(fd_);
    } else {
      close(fd_);
    }
    fd_ = -1;
  }
}

bool FlDrmDisplay::Initialize(FlDrmSeat* seat) {
  seat_ = seat;
  const char* device = getenv("FLUTTER_DRM_DEVICE");
  if (!device) {
    device = "/dev/dri/card0";
  }

  fd_ = seat_ ? seat_->OpenDevice(device, O_RDWR)
              : open(device, O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    fprintf(stderr, "[DRM] Failed to open %s\n", device);
    return false;
  }

  drmModeRes* resources = drmModeGetResources(fd_);
  if (!resources) {
    fprintf(stderr, "[DRM] drmModeGetResources failed\n");
    return false;
  }

  // Appended to at hotplug while other threads index into them: keep
  // capacity fixed so entries never move (see RescanConnectors).
  outputs_.reserve(16);
  saved_crtcs_.reserve(16);

  const char* connector_filter = getenv("FLUTTER_DRM_CONNECTOR");
  const char* mode_request = getenv("FLUTTER_DRM_MODE");
  uint32_t used_crtcs = 0;

  // Enumerate every connected output and assign each a conflict-free CRTC.
  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* c = drmModeGetConnector(fd_, resources->connectors[i]);
    if (!c) {
      continue;
    }
    if (c->connection != DRM_MODE_CONNECTED || c->count_modes == 0) {
      drmModeFreeConnector(c);
      continue;
    }

    FlDrmOutput out;
    ConnectorName(c, out.name, sizeof(out.name));

    if (connector_filter && strcmp(out.name, connector_filter) != 0) {
      drmModeFreeConnector(c);
      continue;
    }

    out.connector_id = c->connector_id;
    if (!SelectMode(c, mode_request, &out.mode)) {
      drmModeFreeConnector(c);
      continue;
    }
    out.crtc_id = AllocCrtc(fd_, resources, c, &used_crtcs);
    if (out.crtc_id == 0) {
      fprintf(stderr, "[DRM] %s: no free CRTC available, skipping\n", out.name);
      drmModeFreeConnector(c);
      continue;
    }

    fprintf(stderr, "[DRM] output %s: connector=%u crtc=%u mode=%ux%u@%u\n",
            out.name, out.connector_id, out.crtc_id, out.mode.hdisplay,
            out.mode.vdisplay, out.mode.vrefresh);
    outputs_.push_back(out);
    drmModeFreeConnector(c);
  }

  drmModeFreeResources(resources);

  if (outputs_.empty()) {
    fprintf(stderr, "[DRM] No connected connector found\n");
    return false;
  }

  // Primary output: the filter-matched one, else the largest by pixel count
  // (so a real panel wins over a small forced/fallback output).
  primary_ = 0;
  if (!connector_filter) {
    uint64_t best = 0;
    for (size_t k = 0; k < outputs_.size(); k++) {
      uint64_t pixels = (uint64_t)outputs_[k].mode.hdisplay *
                        outputs_[k].mode.vdisplay;
      if (pixels > best) {
        best = pixels;
        primary_ = k;
      }
    }
  }

  // Save every output's CRTC state for restore on shutdown — each output is
  // modeset by its own swap chain, so each must be put back on exit.
  saved_crtcs_.resize(outputs_.size(), nullptr);
  for (size_t k = 0; k < outputs_.size(); k++) {
    saved_crtcs_[k] = drmModeGetCrtc(fd_, outputs_[k].crtc_id);
  }

  fprintf(stderr,
          "[DRM] %zu output(s) enumerated; primary=%s connector=%u crtc=%u "
          "%ux%u@%u\n",
          outputs_.size(), primary().name, primary().connector_id,
          primary().crtc_id, primary().mode.hdisplay, primary().mode.vdisplay,
          primary().mode.vrefresh);
  return true;
}

void FlDrmDisplay::RescanConnectors(std::vector<size_t>* added,
                                    std::vector<size_t>* removed) {
  if (fd_ < 0) {
    return;
  }
  drmModeRes* resources = drmModeGetResources(fd_);
  if (!resources) {
    return;
  }

  const char* connector_filter = getenv("FLUTTER_DRM_CONNECTOR");
  const char* mode_request = getenv("FLUTTER_DRM_MODE");

  // CRTCs claimed by live outputs (dead slots free theirs for reuse).
  uint32_t used_crtcs = 0;
  for (const FlDrmOutput& out : outputs_) {
    if (!out.alive) {
      continue;
    }
    for (int j = 0; j < resources->count_crtcs; j++) {
      if (resources->crtcs[j] == out.crtc_id) {
        used_crtcs |= 1u << j;
      }
    }
  }

  for (int i = 0; i < resources->count_connectors; i++) {
    drmModeConnector* c = drmModeGetConnector(fd_, resources->connectors[i]);
    if (!c) {
      continue;
    }
    bool connected =
        c->connection == DRM_MODE_CONNECTED && c->count_modes > 0;

    // A connector we already track?
    int slot = -1;
    for (size_t k = 0; k < outputs_.size(); k++) {
      if (outputs_[k].connector_id == c->connector_id) {
        slot = (int)k;
        break;
      }
    }

    if (slot >= 0 && outputs_[slot].alive && !connected) {
      // Disconnected. The primary is never torn down — its implicit view
      // cannot be removed from the engine.
      if ((size_t)slot == primary_) {
        fprintf(stderr,
                "[DRM] %s (primary) disconnected — kept until restart\n",
                outputs_[slot].name);
      } else {
        fprintf(stderr, "[DRM] %s disconnected — removing output %d\n",
                outputs_[slot].name, slot);
        outputs_[slot].alive = false;
        if (removed) {
          removed->push_back((size_t)slot);
        }
      }
      drmModeFreeConnector(c);
      continue;
    }

    if (!connected || (slot >= 0 && outputs_[slot].alive)) {
      // Still absent, or still connected — nothing to do.
      drmModeFreeConnector(c);
      continue;
    }

    // Newly connected (or a dead slot's connector returning). Build the
    // output, reviving its old slot / the first dead slot when possible so
    // plug cycles don't grow the table.
    FlDrmOutput out;
    ConnectorName(c, out.name, sizeof(out.name));
    if (connector_filter && strcmp(out.name, connector_filter) != 0) {
      drmModeFreeConnector(c);
      continue;
    }
    out.connector_id = c->connector_id;
    if (!SelectMode(c, mode_request, &out.mode)) {
      drmModeFreeConnector(c);
      continue;
    }
    out.crtc_id = AllocCrtc(fd_, resources, c, &used_crtcs);
    if (out.crtc_id == 0) {
      fprintf(stderr, "[DRM] %s: no free CRTC available, skipping\n",
              out.name);
      drmModeFreeConnector(c);
      continue;
    }

    if (slot < 0) {
      for (size_t k = 0; k < outputs_.size(); k++) {
        if (!outputs_[k].alive) {
          slot = (int)k;
          break;
        }
      }
    }
    if (slot < 0) {
      if (outputs_.size() >= outputs_.capacity()) {
        // Never reallocate: other threads index into outputs_ (see reserve).
        fprintf(stderr, "[DRM] output table full, skipping %s\n", out.name);
        drmModeFreeConnector(c);
        continue;
      }
      outputs_.push_back(out);
      saved_crtcs_.push_back(drmModeGetCrtc(fd_, out.crtc_id));
      slot = (int)(outputs_.size() - 1);
    } else {
      outputs_[slot] = out;
      if (saved_crtcs_[slot]) {
        drmModeFreeCrtc(saved_crtcs_[slot]);
      }
      saved_crtcs_[slot] = drmModeGetCrtc(fd_, out.crtc_id);
    }

    fprintf(stderr,
            "[DRM] hotplug output %s: connector=%u crtc=%u mode=%ux%u@%u "
            "(slot %d)\n",
            out.name, out.connector_id, out.crtc_id, out.mode.hdisplay,
            out.mode.vdisplay, out.mode.vrefresh, slot);
    if (added) {
      added->push_back((size_t)slot);
    }
    drmModeFreeConnector(c);
  }

  drmModeFreeResources(resources);
}

void FlDrmDisplay::RestoreCrtc() {
  if (fd_ < 0) {
    return;
  }
  for (size_t k = 0; k < saved_crtcs_.size() && k < outputs_.size(); k++) {
    drmModeCrtc* saved = saved_crtcs_[k];
    if (!saved) {
      continue;
    }
    if (saved->buffer_id) {
      uint32_t connector = outputs_[k].connector_id;
      drmModeSetCrtc(fd_, saved->crtc_id, saved->buffer_id, saved->x, saved->y,
                     &connector, 1, &saved->mode);
    } else {
      // The CRTC was off before we claimed it — turn it back off.
      drmModeSetCrtc(fd_, saved->crtc_id, 0, 0, 0, nullptr, 0, nullptr);
    }
  }
}

}  // namespace flutter
