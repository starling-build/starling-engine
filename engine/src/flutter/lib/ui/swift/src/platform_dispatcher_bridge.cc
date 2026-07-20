// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/platform_dispatcher_bridge.h"

#include <cstring>

#include "include/swift_bridge_engine_registry.h"

namespace flutter::swift_bridge {

/// Pimpl implementation.
///
/// **Status:** PARTIAL - ScheduleFrame() is wired via SwiftBridgeEngineRegistry;
/// other methods remain stubs.
///
/// DIFFERENCE FROM DART: Dart uses UIDartState::Current() to access
/// the PlatformConfiguration singleton. Swift uses SwiftBridgeEngineRegistry
/// to access engine callbacks set during shell startup.
/// REASON: No Dart VM available in Swift; engine connection is
/// established through the global registry.
struct PlatformDispatcherImpl {
  int engine_id = 0;

  PlatformDispatcherImpl() {}
};

PlatformDispatcherBridge::PlatformDispatcherBridge() {
  impl_ = new PlatformDispatcherImpl();
}

PlatformDispatcherBridge::~PlatformDispatcherBridge() {
  delete impl_;
}

int PlatformDispatcherBridge::GetEngineId() const {
  // Stub - returns 0 (no engine connected yet)
  // In full implementation, this would query the engine via
  // PlatformConfiguration or RuntimeController.
  return impl_->engine_id;
}

void PlatformDispatcherBridge::ScheduleFrame() {
  // Request a new frame from the engine via the global registry callback.
  // This triggers: ScheduleFrame -> Animator -> VSync -> BeginFrame -> onBeginFrame.
  const auto& callback =
      SwiftBridgeEngineRegistry::GetScheduleFrameCallback();
  if (callback) {
    callback(/*regenerate_layer_trees=*/true);
  }
}

const char* PlatformDispatcherBridge::GetDefaultRouteName() const {
  // Stub - returns "/" (default route)
  // In full implementation, this would call:
  //   client->DefaultRouteName()
  return "/";
}

}  // namespace flutter::swift_bridge
