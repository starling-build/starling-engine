// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_DISPLAY_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_DISPLAY_H_

#include <stdint.h>
#include <vector>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace flutter {

class FlDrmSeat;

// One connected output: a connector + the CRTC assigned to drive it + its mode.
struct FlDrmOutput {
  uint32_t connector_id = 0;
  uint32_t crtc_id = 0;
  drmModeModeInfo mode = {};
  char name[32] = {0};  // e.g. "HDMI-A-1", "DP-1"
  // The panel's physical size in millimetres, as EDID reports it. Zero when
  // the connector carries no EDID — every virtual display, and some KVMs —
  // so a reader must treat 0 as "unknown", never as a real measurement.
  uint32_t mm_width = 0;
  uint32_t mm_height = 0;
  // Cleared when the connector disconnects (hotplug). The slot stays —
  // indexes are stable — and is revived if the connector returns.
  bool alive = true;

  uint32_t width() const { return mode.hdisplay; }
  uint32_t height() const { return mode.vdisplay; }
};

// Enumerates the DRM device's connected outputs and assigns each a conflict-free
// CRTC. Every output gets its own swap chain (per-output scanout); the engine's
// implicit view renders to the primary, so the single-output accessors below
// forward to it and behavior is unchanged when there is one output.
class FlDrmDisplay {
 public:
  FlDrmDisplay();
  ~FlDrmDisplay();

  // Opens the DRM device (through the seat, which grants master in libseat
  // mode) and enumerates connected outputs.
  // FLUTTER_DRM_DEVICE selects the device (default /dev/dri/card0).
  // FLUTTER_DRM_CONNECTOR, if set, restricts to that single connector.
  bool Initialize(FlDrmSeat* seat);

  int fd() const { return fd_; }

  // --- Multi-output ---
  size_t num_outputs() const { return outputs_.size(); }
  const FlDrmOutput& output(size_t i) const { return outputs_[i]; }
  size_t primary_index() const { return primary_; }
  const FlDrmOutput& primary() const { return outputs_[primary_]; }

  // Runtime primary reassignment (fl_drm_view_set_primary_output). Platform
  // thread only, like RescanConnectors — the "primary is never removed"
  // rule in RescanConnectors starts protecting the new output and releases
  // the old one from the moment this returns.
  void set_primary(size_t i) {
    if (i < outputs_.size() && outputs_[i].alive) {
      primary_ = i;
    }
  }

  // --- Backward-compatible single-output accessors (forward to primary) ---
  uint32_t connector_id() const { return primary().connector_id; }
  uint32_t crtc_id() const { return primary().crtc_id; }
  uint32_t width() const { return primary().mode.hdisplay; }
  uint32_t height() const { return primary().mode.vdisplay; }
  const drmModeModeInfo& mode() const { return primary().mode; }

  // Re-enumerate connectors after a hotplug uevent. Indexes are stable —
  // everything maps outputs by index. A newly connected connector revives
  // its old slot (same connector returning) or takes the first dead slot,
  // else appends; its index goes to `added`. A connector that disconnected
  // is marked !alive and its index goes to `removed` — the caller tears
  // down its resources. The primary is never removed (marked !alive but
  // not reported) — the engine's implicit view can't be removed.
  void RescanConnectors(std::vector<size_t>* added,
                        std::vector<size_t>* removed);

  // Restore every output's original CRTC state (call before close).
  void RestoreCrtc();

 private:
  int fd_ = -1;
  FlDrmSeat* seat_ = nullptr;
  std::vector<FlDrmOutput> outputs_;
  size_t primary_ = 0;
  // Original CRTC state per output (parallel to outputs_), restored on
  // shutdown. A CRTC that was off (buffer_id == 0) is disabled again.
  std::vector<drmModeCrtc*> saved_crtcs_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_DISPLAY_H_
