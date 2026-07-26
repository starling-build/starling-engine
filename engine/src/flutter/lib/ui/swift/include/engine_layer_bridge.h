// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_ENGINE_LAYER_BRIDGE_H_
#define FLUTTER_SWIFT_ENGINE_LAYER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class EngineLayerBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainEngineLayerBridge(
    flutter::swift_bridge::EngineLayerBridge* p) noexcept;
void ReleaseEngineLayerBridge(
    flutter::swift_bridge::EngineLayerBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct EngineLayerImpl;

/// C++ bridge wrapping Flutter's _NativeEngineLayer class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:2827-2835`
/// **Original Name:** `_NativeEngineLayer`
///
/// This bridge wraps a std::shared_ptr<flutter::ContainerLayer> which represents
/// a retained engine layer that the framework can hold across frames.
///
/// Architecture:
/// - Swift NativeEngineLayer class wraps EngineLayerBridge
/// - EngineLayerBridge holds std::shared_ptr<ContainerLayer> via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - Created by SceneBuilder push methods (future compositing migration)
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainEngineLayerBridge, ReleaseEngineLayerBridge)
        EngineLayerBridge
    : public IntrusiveReferenceCounted<EngineLayerBridge> {
 public:
  /// Creates an EngineLayerBridge wrapping a ContainerLayer.
  ///
  /// The layer pointer is passed as void* to avoid exposing Flutter engine
  /// types in the Swift-compatible header. The caller must pass a pointer
  /// to a std::shared_ptr<flutter::ContainerLayer>.
  ///
  /// @param layer_ptr Opaque pointer to std::shared_ptr<ContainerLayer>
  explicit EngineLayerBridge(const void* layer_ptr);

  /// Releases the underlying ContainerLayer.
  ///
  /// **Dart Source:** `painting.dart:2832-2834`
  /// **Original:** `@Native<Void Function(Pointer<Void>)>(symbol: 'EngineLayer::dispose')`
  ///
  /// After calling this, the layer is no longer usable and should not be
  /// passed as an `oldLayer` to SceneBuilder methods.
  void Dispose();

  /// Whether Dispose() has been called.
  bool IsDisposed() const;

  /// Returns an opaque pointer to the internal shared_ptr<ContainerLayer>.
  ///
  /// Used by SceneBuilder bridge to pass the layer to push methods.
  /// Returns nullptr if disposed.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetLayerPtr() const;

  ~EngineLayerBridge();

 private:
  EngineLayerBridge(const EngineLayerBridge&) = delete;
  EngineLayerBridge& operator=(const EngineLayerBridge&) = delete;

  // Pimpl - holds the actual Flutter types (shared_ptr<ContainerLayer>)
  EngineLayerImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainEngineLayerBridge(
    flutter::swift_bridge::EngineLayerBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseEngineLayerBridge(
    flutter::swift_bridge::EngineLayerBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_ENGINE_LAYER_BRIDGE_H_
