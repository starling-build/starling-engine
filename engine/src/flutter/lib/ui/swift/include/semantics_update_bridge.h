// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_SEMANTICS_UPDATE_BRIDGE_H_
#define FLUTTER_SWIFT_SEMANTICS_UPDATE_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class SemanticsUpdateBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainSemanticsUpdateBridge(
    flutter::swift_bridge::SemanticsUpdateBridge* p) noexcept;
void ReleaseSemanticsUpdateBridge(
    flutter::swift_bridge::SemanticsUpdateBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct SemanticsUpdateImpl;

/// C++ bridge wrapping Flutter's SemanticsUpdate.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/semantics.dart:2083-2113`
/// **Original:** `abstract class SemanticsUpdate` / `_NativeSemanticsUpdate`
///
/// Holds the accumulated semantics node updates and custom accessibility
/// action updates produced by SemanticsUpdateBuilderBridge::Build().
/// This object is passed to PlatformDispatcher.updateSemantics.
///
/// Architecture:
/// - Swift NativeSemanticsUpdate class wraps SemanticsUpdateBridge
/// - SemanticsUpdateBridge holds SemanticsNodeUpdates and
///   CustomAccessibilityActionUpdates via pimpl
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainSemanticsUpdateBridge,
                           ReleaseSemanticsUpdateBridge)
        SemanticsUpdateBridge
    : public IntrusiveReferenceCounted<SemanticsUpdateBridge> {
 public:
  /// Creates a SemanticsUpdateBridge from internal data.
  ///
  /// This constructor is called by SemanticsUpdateBuilderBridge::Build().
  /// The opaque_nodes and opaque_actions are void* pointers to
  /// SemanticsNodeUpdates and CustomAccessibilityActionUpdates respectively.
  ///
  /// **Dart Source:** `semantics.dart:2100-2102`
  /// **Original:** `_NativeSemanticsUpdate._()` (engine-created)
  SemanticsUpdateBridge(void* opaque_nodes, void* opaque_actions);

  ~SemanticsUpdateBridge();

  /// Releases the resources held by this SemanticsUpdate.
  ///
  /// **Dart Source:** `semantics.dart:2108-2112`
  /// **Original:** `@Native dispose()`
  ///
  /// After calling Dispose(), this object should not be used further.
  void Dispose();

  /// Returns an opaque pointer to the internal SemanticsNodeUpdates.
  /// Used by PlatformDispatcher bridge to access the node data.
  const void* GetNodesPtr() const;

  /// Returns an opaque pointer to the internal CustomAccessibilityActionUpdates.
  /// Used by PlatformDispatcher bridge to access the action data.
  const void* GetActionsPtr() const;

 private:
  SemanticsUpdateBridge(const SemanticsUpdateBridge&) = delete;
  SemanticsUpdateBridge& operator=(const SemanticsUpdateBridge&) = delete;

  // Pimpl - holds SemanticsNodeUpdates and CustomAccessibilityActionUpdates
  SemanticsUpdateImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainSemanticsUpdateBridge(
    flutter::swift_bridge::SemanticsUpdateBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseSemanticsUpdateBridge(
    flutter::swift_bridge::SemanticsUpdateBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_SEMANTICS_UPDATE_BRIDGE_H_
