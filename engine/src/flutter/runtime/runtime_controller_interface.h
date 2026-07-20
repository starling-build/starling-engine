// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_RUNTIME_RUNTIME_CONTROLLER_INTERFACE_H_
#define FLUTTER_RUNTIME_RUNTIME_CONTROLLER_INTERFACE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "flutter/fml/closure.h"
#include "flutter/fml/mapping.h"
#include "flutter/fml/time/time_delta.h"
#include "flutter/fml/time/time_point.h"
#include "flutter/lib/ui/semantics/semantics_node.h"
#include "flutter/lib/ui/window/platform_message.h"
#include "flutter/lib/ui/window/pointer_data_packet.h"
#include "flutter/lib/ui/window/view_focus.h"
#include "flutter/lib/ui/window/viewport_metrics.h"
#include "flutter/shell/common/display.h"
#include "third_party/dart/runtime/include/dart_api.h"
#include "third_party/tonic/logging/dart_error.h"

namespace flutter {

class IsolateConfiguration;
class NativeAssetsManager;
struct Settings;

//------------------------------------------------------------------------------
/// @brief      An abstract interface for RuntimeController that defines the
///             contract between the Engine and the runtime implementation.
///
///             This interface allows the Engine to work with different runtime
///             implementations (e.g., Dart-based RuntimeController or a
///             Swift-based runtime controller) without depending on
///             implementation-specific details.
///
///             Methods that are specific to a particular runtime (e.g., Dart)
///             have default implementations that return sensible no-op values,
///             so that alternative runtime implementations need only override
///             the methods they care about.
///
class RuntimeControllerInterface {
 public:
  /// A callback that's invoked after the runtime controller attempts to
  /// add a view.
  using AddViewCallback = std::function<void(bool added)>;

  virtual ~RuntimeControllerInterface() = default;

  //--------------------------------------------------------------------------
  // Frame scheduling
  //--------------------------------------------------------------------------

  /// @brief      Notifies the running runtime that it should start generating a
  ///             new frame.
  virtual bool BeginFrame(fml::TimePoint frame_time,
                          uint64_t frame_number) = 0;

  /// @brief      Reports frame timing data back to the runtime.
  virtual bool ReportTimings(std::vector<int64_t> timings) = 0;

  //--------------------------------------------------------------------------
  // View management
  //--------------------------------------------------------------------------

  /// @brief      Notify the runtime that a new view is available.
  virtual void AddView(int64_t view_id,
                       const ViewportMetrics& view_metrics,
                       AddViewCallback callback) = 0;

  /// @brief      Notify the runtime that a view is no longer available.
  virtual bool RemoveView(int64_t view_id) = 0;

  /// @brief      Notify the runtime that the focus state of a native view has
  ///             changed.
  virtual bool SendViewFocusEvent(const ViewFocusEvent& event) = 0;

  /// @brief      Forward the specified viewport metrics to the running runtime.
  virtual bool SetViewportMetrics(int64_t view_id,
                                  const ViewportMetrics& metrics) = 0;

  /// @brief      Forward the specified display metrics to the running runtime.
  virtual bool SetDisplays(const std::vector<DisplayData>& displays) = 0;

  //--------------------------------------------------------------------------
  // Input
  //--------------------------------------------------------------------------

  /// @brief      Dispatch the specified pointer data message to the running
  ///             runtime.
  virtual bool DispatchPointerDataPacket(const PointerDataPacket& packet) = 0;

  /// @brief      Dispatch the semantics action to the specified accessibility
  ///             node.
  virtual bool DispatchSemanticsAction(int64_t view_id,
                                       int32_t node_id,
                                       SemanticsAction action,
                                       fml::MallocMapping args) = 0;

  //--------------------------------------------------------------------------
  // Configuration
  //--------------------------------------------------------------------------

  /// @brief      Notifies the running runtime about whether the semantics tree
  ///             should be generated or not.
  virtual bool SetSemanticsEnabled(bool enabled) = 0;

  /// @brief      Forward the preference of accessibility features that must be
  ///             enabled in the semantics tree to the running runtime.
  virtual bool SetAccessibilityFeatures(int32_t flags) = 0;

  /// @brief      Forward the specified locale data to the running runtime.
  virtual bool SetLocales(const std::vector<std::string>& locale_data) = 0;

  /// @brief      Forward the user settings data to the running runtime.
  virtual bool SetUserSettingsData(const std::string& data) = 0;

  /// @brief      Forward the initial lifecycle state data to the running
  ///             runtime.
  virtual bool SetInitialLifecycleState(const std::string& data) = 0;

  //--------------------------------------------------------------------------
  // Platform messages
  //--------------------------------------------------------------------------

  /// @brief      Dispatch the specified platform message to running runtime.
  virtual bool DispatchPlatformMessage(
      std::unique_ptr<PlatformMessage> message) = 0;

  //--------------------------------------------------------------------------
  // Lifecycle - Dart-specific with sensible defaults
  //--------------------------------------------------------------------------

  /// @brief      Returns if the root isolate is running.
  ///
  ///             Default returns false (no Dart isolate).
  virtual bool IsRootIsolateRunning() const { return false; }

  /// @brief      Notify the runtime that no frame workloads are expected on the
  ///             UI task runner till the specified deadline.
  ///
  ///             Default returns false (no-op).
  virtual bool NotifyIdle(fml::TimeDelta deadline) { return false; }

  /// @brief      Gets the main port identifier of the root isolate.
  ///
  ///             Default returns ILLEGAL_PORT.
  virtual Dart_Port GetMainPort() { return ILLEGAL_PORT; }

  /// @brief      Gets the debug name of the root isolate.
  ///
  ///             Default returns an empty string.
  virtual std::string GetIsolateName() { return ""; }

  /// @brief      Returns if the root isolate has any live receive ports.
  ///
  ///             Default returns false.
  virtual bool HasLivePorts() { return false; }

  /// @brief      Returns if the root isolate has any pending microtasks.
  ///
  ///             Default returns false.
  virtual bool HasPendingMicrotasks() { return false; }

  /// @brief      Get the last error encountered by the microtask queue.
  ///
  ///             Default returns tonic::kNoError.
  virtual tonic::DartErrorHandleType GetLastError() { return tonic::kNoError; }

  /// @brief      Get the service ID of the root isolate if running.
  ///
  ///             Default returns std::nullopt.
  virtual std::optional<std::string> GetRootIsolateServiceID() const {
    return std::nullopt;
  }

  /// @brief      Get the return code specified by the root isolate.
  ///
  ///             Default returns std::nullopt.
  virtual std::optional<uint32_t> GetRootIsolateReturnCode() {
    return std::nullopt;
  }

  /// @brief      Get an identifier that represents the Dart isolate group the
  ///             root isolate is in.
  ///
  ///             Default returns 0.
  virtual uint64_t GetRootIsolateGroup() const { return 0; }

  /// @brief      Launches the root isolate using the runtime data associated
  ///             with this runtime controller.
  ///
  ///             Default returns false (not supported).
  [[nodiscard]] virtual bool LaunchRootIsolate(
      const Settings& settings,
      const fml::closure& root_isolate_create_callback,
      std::optional<std::string> dart_entrypoint,
      std::optional<std::string> dart_entrypoint_library,
      const std::vector<std::string>& dart_entrypoint_args,
      std::unique_ptr<IsolateConfiguration> isolate_configuration,
      std::shared_ptr<NativeAssetsManager> native_assets_manager,
      std::optional<int64_t> engine_id);

  /// @brief      Loads the Dart shared library into the Dart VM.
  ///
  ///             Default is a no-op.
  virtual void LoadDartDeferredLibrary(
      intptr_t loading_unit_id,
      std::unique_ptr<const fml::Mapping> snapshot_data,
      std::unique_ptr<const fml::Mapping> snapshot_instructions);

  /// @brief      Indicates to the dart VM that the request to load a deferred
  ///             library with the specified loading unit id has failed.
  ///
  ///             Default is a no-op.
  virtual void LoadDartDeferredLibraryError(intptr_t loading_unit_id,
                                            const std::string error_message,
                                            bool transient) {}

  /// @brief      Shuts down all registered platform isolates.
  ///
  ///             Default is a no-op.
  virtual void ShutdownPlatformIsolates() {}

  /// @brief      Flushes the microtask queue of the root isolate.
  ///
  ///             Default is a no-op.
  virtual void FlushMicrotaskQueue() {}

  /// @brief      Sets the root isolate owner to the current thread.
  ///
  ///             Default is a no-op.
  virtual void SetRootIsolateOwnerToCurrentThread() {}
};

}  // namespace flutter

#endif  // FLUTTER_RUNTIME_RUNTIME_CONTROLLER_INTERFACE_H_
