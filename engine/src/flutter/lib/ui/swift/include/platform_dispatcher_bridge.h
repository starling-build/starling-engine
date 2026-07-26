// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_PLATFORM_DISPATCHER_BRIDGE_H_
#define FLUTTER_SWIFT_PLATFORM_DISPATCHER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class PlatformDispatcherBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainPlatformDispatcherBridge(
    flutter::swift_bridge::PlatformDispatcherBridge* p) noexcept;
void ReleasePlatformDispatcherBridge(
    flutter::swift_bridge::PlatformDispatcherBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct PlatformDispatcherImpl;

/// C++ bridge for PlatformDispatcher.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/platform_dispatcher.dart:104-1553`
/// **Status:** PARTIAL - ScheduleFrame() wired via SwiftBridgeEngineRegistry;
/// other methods remain stubs.
///
/// DIFFERENCE FROM DART: The Dart version uses PlatformConfigurationNativeApi
/// which depends on UIDartState (Dart VM). The Swift bridge connects to the
/// engine via SwiftBridgeEngineRegistry callbacks set during shell startup.
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainPlatformDispatcherBridge,
                           ReleasePlatformDispatcherBridge)
        PlatformDispatcherBridge
    : public IntrusiveReferenceCounted<PlatformDispatcherBridge> {
 public:
  /// Creates a PlatformDispatcherBridge.
  PlatformDispatcherBridge();

  /// Returns the engine ID, or 0 if not set.
  ///
  /// **Dart Source:** `platform_dispatcher.dart:308-311`
  /// **Original:** `int get engineId => _engineId;`
  int GetEngineId() const;

  /// Requests a frame to be scheduled.
  ///
  /// **Dart Source:** `platform_dispatcher.dart:866-875`
  /// **Original:** `void scheduleFrame() => _scheduleFrame();`
  ///
  /// Calls RuntimeDelegate::ScheduleFrame() via SwiftBridgeEngineRegistry.
  /// No-op if the engine callback has not been registered yet.
  void ScheduleFrame();

  /// Returns the default route name.
  ///
  /// **Dart Source:** `platform_dispatcher.dart:940-960`
  /// **Original:** `String get defaultRouteName => _defaultRouteName();`
  const char* GetDefaultRouteName() const;

  ~PlatformDispatcherBridge();

 private:
  PlatformDispatcherBridge(const PlatformDispatcherBridge&) = delete;
  PlatformDispatcherBridge& operator=(const PlatformDispatcherBridge&) = delete;

  // Pimpl - holds any future engine references
  PlatformDispatcherImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainPlatformDispatcherBridge(
    flutter::swift_bridge::PlatformDispatcherBridge* p) noexcept {
  p->Retain();
}
inline void ReleasePlatformDispatcherBridge(
    flutter::swift_bridge::PlatformDispatcherBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_PLATFORM_DISPATCHER_BRIDGE_H_
