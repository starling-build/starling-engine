// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_RUNTIME_CONTROLLER_H_
#define FLUTTER_SWIFT_RUNTIME_CONTROLLER_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "flutter/fml/mapping.h"
#include "flutter/lib/ui/swift/include/swift_runtime_callbacks.h"
#include "flutter/runtime/runtime_controller_interface.h"

namespace flutter {

/// A runtime controller that delegates Shell-to-Framework calls to a Swift
/// runtime via a C function-pointer table (`SwiftRuntimeCallbacks`).
///
/// This class implements `RuntimeControllerInterface` without any Dart VM
/// dependency and without any Swift compiler dependency. The callbacks are
/// filled in by the Swift side at startup.
class SwiftRuntimeController final : public RuntimeControllerInterface {
 public:
  explicit SwiftRuntimeController(SwiftRuntimeCallbacks callbacks);
  ~SwiftRuntimeController() override;

  // Non-copyable, non-movable.
  SwiftRuntimeController(const SwiftRuntimeController&) = delete;
  SwiftRuntimeController& operator=(const SwiftRuntimeController&) = delete;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — Frame scheduling
  //--------------------------------------------------------------------------

  bool BeginFrame(fml::TimePoint frame_time, uint64_t frame_number) override;

  bool ReportTimings(std::vector<int64_t> timings) override;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — View management
  //--------------------------------------------------------------------------

  void AddView(int64_t view_id,
               const ViewportMetrics& view_metrics,
               AddViewCallback callback) override;

  bool RemoveView(int64_t view_id) override;

  bool SendViewFocusEvent(const ViewFocusEvent& event) override;

  bool SetViewportMetrics(int64_t view_id,
                          const ViewportMetrics& metrics) override;

  bool SetDisplays(const std::vector<DisplayData>& displays) override;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — Input
  //--------------------------------------------------------------------------

  bool DispatchPointerDataPacket(const PointerDataPacket& packet) override;

  bool DispatchSemanticsAction(int64_t view_id,
                               int32_t node_id,
                               SemanticsAction action,
                               fml::MallocMapping args) override;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — Configuration
  //--------------------------------------------------------------------------

  bool SetSemanticsEnabled(bool enabled) override;

  bool SetAccessibilityFeatures(int32_t flags) override;

  bool SetLocales(const std::vector<std::string>& locale_data) override;

  bool SetUserSettingsData(const std::string& data) override;

  bool SetInitialLifecycleState(const std::string& data) override;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — Platform messages
  //--------------------------------------------------------------------------

  bool DispatchPlatformMessage(
      std::unique_ptr<PlatformMessage> message) override;

  //--------------------------------------------------------------------------
  // RuntimeControllerInterface — Lifecycle overrides
  //--------------------------------------------------------------------------

  bool IsRootIsolateRunning() const override;

  bool NotifyIdle(fml::TimeDelta deadline) override;

 private:
  SwiftRuntimeCallbacks callbacks_;

  // Response ID counter for platform messages (0 means "no response expected").
  int32_t next_response_id_ = 1;

  // Maps response IDs to pending platform message responses.
  std::unordered_map<int32_t, fml::RefPtr<PlatformMessageResponse>>
      pending_responses_;
};

}  // namespace flutter

#endif  // FLUTTER_SWIFT_RUNTIME_CONTROLLER_H_
