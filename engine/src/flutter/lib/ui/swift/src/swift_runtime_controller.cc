// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/lib/ui/swift/include/swift_runtime_controller.h"

#include "flutter/fml/logging.h"

#include <cstring>
#include <vector>

namespace flutter {

SwiftRuntimeController::SwiftRuntimeController(SwiftRuntimeCallbacks callbacks)
    : callbacks_(callbacks) {}

SwiftRuntimeController::~SwiftRuntimeController() = default;

//------------------------------------------------------------------------------
// Frame scheduling
//------------------------------------------------------------------------------

bool SwiftRuntimeController::BeginFrame(fml::TimePoint frame_time,
                                        uint64_t frame_number) {
  int64_t microseconds = frame_time.ToEpochDelta().ToMicroseconds();

  if (callbacks_.begin_frame) {
    callbacks_.begin_frame(callbacks_.context, microseconds, frame_number);
  }

  if (callbacks_.draw_frame) {
    callbacks_.draw_frame(callbacks_.context);
  }

  return true;
}

bool SwiftRuntimeController::ReportTimings(std::vector<int64_t> timings) {
  if (callbacks_.report_timings) {
    callbacks_.report_timings(callbacks_.context, timings.data(),
                              timings.size());
  }
  return true;
}

//------------------------------------------------------------------------------
// View management
//------------------------------------------------------------------------------

void SwiftRuntimeController::AddView(int64_t view_id,
                                     const ViewportMetrics& view_metrics,
                                     AddViewCallback callback) {
  if (callbacks_.add_view) {
    callbacks_.add_view(callbacks_.context, view_id,
                        view_metrics.physical_width,
                        view_metrics.physical_height,
                        view_metrics.device_pixel_ratio,
                        static_cast<int64_t>(view_metrics.display_id));
  }

  if (callback) {
    callback(true);
  }
}

bool SwiftRuntimeController::RemoveView(int64_t view_id) {
  if (callbacks_.remove_view) {
    callbacks_.remove_view(callbacks_.context, view_id);
  }
  return true;
}

bool SwiftRuntimeController::SendViewFocusEvent(const ViewFocusEvent& event) {
  if (callbacks_.send_view_focus_event) {
    callbacks_.send_view_focus_event(callbacks_.context, event.view_id(),
                                     static_cast<int32_t>(event.state()),
                                     static_cast<int32_t>(event.direction()));
  }
  return true;
}

bool SwiftRuntimeController::SetViewportMetrics(int64_t view_id,
                                                const ViewportMetrics& m) {
  if (callbacks_.set_viewport_metrics) {
    callbacks_.set_viewport_metrics(
        callbacks_.context, view_id, m.physical_width, m.physical_height,
        m.device_pixel_ratio, m.physical_padding_top, m.physical_padding_right,
        m.physical_padding_bottom, m.physical_padding_left,
        m.physical_view_inset_top, m.physical_view_inset_right,
        m.physical_view_inset_bottom, m.physical_view_inset_left,
        m.physical_system_gesture_inset_top,
        m.physical_system_gesture_inset_right,
        m.physical_system_gesture_inset_bottom,
        m.physical_system_gesture_inset_left, m.physical_touch_slop,
        static_cast<int64_t>(m.display_id));
  }
  return true;
}

bool SwiftRuntimeController::SetDisplays(
    const std::vector<DisplayData>& displays) {
  if (callbacks_.set_displays) {
    // Encode display data as a flat array of doubles, 5 values per display:
    // [id, devicePixelRatio, width, height, refreshRate, ...]
    std::vector<double> encoded;
    encoded.reserve(displays.size() * 5);
    for (const auto& d : displays) {
      encoded.push_back(static_cast<double>(d.id));
      encoded.push_back(d.pixel_ratio);
      encoded.push_back(d.width);
      encoded.push_back(d.height);
      encoded.push_back(d.refresh_rate);
    }
    callbacks_.set_displays(callbacks_.context, encoded.data(), encoded.size());
  }
  return true;
}

//------------------------------------------------------------------------------
// Input
//------------------------------------------------------------------------------

bool SwiftRuntimeController::DispatchPointerDataPacket(
    const PointerDataPacket& packet) {
  if (callbacks_.dispatch_pointer_data_packet) {
    const auto& data = packet.data();
    callbacks_.dispatch_pointer_data_packet(callbacks_.context, data.data(),
                                            data.size());
  }
  return true;
}

bool SwiftRuntimeController::DispatchSemanticsAction(int64_t view_id,
                                                     int32_t node_id,
                                                     SemanticsAction action,
                                                     fml::MallocMapping args) {
  if (callbacks_.dispatch_semantics_action) {
    const uint8_t* args_data = args.GetMapping();
    size_t args_size = args.GetSize();
    callbacks_.dispatch_semantics_action(
        callbacks_.context, static_cast<int32_t>(view_id), node_id,
        static_cast<int32_t>(action), args_data, args_size);
  }
  return true;
}

//------------------------------------------------------------------------------
// Configuration
//------------------------------------------------------------------------------

bool SwiftRuntimeController::SetSemanticsEnabled(bool enabled) {
  if (callbacks_.set_semantics_enabled) {
    callbacks_.set_semantics_enabled(callbacks_.context, enabled);
  }
  return true;
}

bool SwiftRuntimeController::SetAccessibilityFeatures(int32_t flags) {
  if (callbacks_.set_accessibility_features) {
    callbacks_.set_accessibility_features(callbacks_.context, flags);
  }
  return true;
}

bool SwiftRuntimeController::SetLocales(
    const std::vector<std::string>& locale_data) {
  if (callbacks_.set_locales) {
    // Build a temporary array of C strings.
    std::vector<const char*> c_strings;
    c_strings.reserve(locale_data.size());
    for (const auto& s : locale_data) {
      c_strings.push_back(s.c_str());
    }
    callbacks_.set_locales(callbacks_.context, c_strings.data(),
                           c_strings.size());
  }
  return true;
}

bool SwiftRuntimeController::SetUserSettingsData(const std::string& data) {
  if (callbacks_.set_user_settings_data) {
    callbacks_.set_user_settings_data(callbacks_.context, data.c_str());
  }
  return true;
}

bool SwiftRuntimeController::SetInitialLifecycleState(
    const std::string& data) {
  if (callbacks_.set_initial_lifecycle_state) {
    callbacks_.set_initial_lifecycle_state(callbacks_.context, data.c_str());
  }
  return true;
}

//------------------------------------------------------------------------------
// Platform messages
//------------------------------------------------------------------------------

bool SwiftRuntimeController::DispatchPlatformMessage(
    std::unique_ptr<PlatformMessage> message) {
  if (!message) {
    return false;
  }

  const std::string& channel = message->channel();

  // Intercept flutter/keydata and dispatch directly via callback,
  // bypassing channelBuffers (same pattern as pointer data).
  // This avoids MainActor isolation issues in DRM mode where the
  // UI task runner is not the main thread.
  if (channel == "flutter/keydata" && callbacks_.dispatch_key_data) {
    if (message->hasData()) {
      const fml::MallocMapping& data = message->data();
      callbacks_.dispatch_key_data(callbacks_.context,
                                    data.GetMapping(), data.GetSize());
    }
    // Auto-complete response with "not handled" (0).
    if (auto response = message->response()) {
      if (!response->is_complete()) {
        response->Complete(
            std::make_unique<fml::DataMapping>(std::vector<uint8_t>{0}));
      }
    }
    return true;
  }

  if (callbacks_.dispatch_platform_message) {
    const uint8_t* data_ptr = nullptr;
    size_t data_size = 0;
    if (message->hasData()) {
      const fml::MallocMapping& data = message->data();
      data_ptr = data.GetMapping();
      data_size = data.GetSize();
    }

    // Pass a response ID so Swift can respond asynchronously in the future.
    int32_t response_id = 0;
    if (auto response = message->response()) {
      response_id = next_response_id_++;
      pending_responses_[response_id] = response;
    }

    callbacks_.dispatch_platform_message(callbacks_.context, channel.c_str(),
                                         data_ptr, data_size, response_id);

    // Clean up any pending response that Swift didn't complete.
    if (response_id != 0) {
      pending_responses_.erase(response_id);
    }
  }

  // Auto-complete any response not yet handled by the Swift callback.
  // This prevents FlutterKeyboardManager and other platform channels from
  // stalling while waiting for a reply.
  if (auto response = message->response()) {
    if (!response->is_complete()) {
      // For flutter/keydata, respond with a single byte 0 (not handled).
      if (channel == "flutter/keydata") {
        response->Complete(
            std::make_unique<fml::DataMapping>(std::vector<uint8_t>{0}));
      } else {
        response->CompleteEmpty();
      }
    }
  }

  return true;
}

//------------------------------------------------------------------------------
// Lifecycle — Swift runtime is always "running"
//------------------------------------------------------------------------------

bool SwiftRuntimeController::IsRootIsolateRunning() const {
  return true;
}

bool SwiftRuntimeController::NotifyIdle(fml::TimeDelta deadline) {
  return true;
}

}  // namespace flutter
