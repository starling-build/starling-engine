// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_SCENE_BRIDGE_H_
#define FLUTTER_SWIFT_SCENE_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class SceneBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainSceneBridge(flutter::swift_bridge::SceneBridge* p) noexcept;
void ReleaseSceneBridge(flutter::swift_bridge::SceneBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl and bridge types
struct SceneImpl;
class ImageBridge;

/// C++ bridge wrapping Flutter's Scene class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/compositing.dart:36-87`
/// **Original Name:** `_NativeScene`
///
/// This bridge wraps a std::shared_ptr<flutter::Layer> which represents the
/// root layer of a composited scene built by SceneBuilder. The scene can be
/// rendered to the screen via FlutterView.render or rasterized to an image.
///
/// Architecture:
/// - Swift NativeScene class wraps SceneBridge
/// - SceneBridge holds std::shared_ptr<Layer> via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - Created by SceneBuilderBridge::Build()
///
/// DIFFERENCE FROM DART: toImage (async rasterization) is not yet available.
/// REASON: The async path requires UIDartState for task runner access and
/// SnapshotDelegate, which are Dart VM specific. A future engine integration
/// layer will provide the async path.
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainSceneBridge, ReleaseSceneBridge)
        SceneBridge
    : public IntrusiveReferenceCounted<SceneBridge> {
 public:
  /// Creates a SceneBridge wrapping a root layer.
  ///
  /// The layer pointer is passed as void* to avoid exposing Flutter engine
  /// types in the Swift-compatible header. The caller must pass a pointer
  /// to a std::shared_ptr<flutter::Layer>.
  ///
  /// @param root_layer_ptr Opaque pointer to std::shared_ptr<flutter::Layer>
  explicit SceneBridge(const void* root_layer_ptr);

  /// Releases the underlying layer tree.
  ///
  /// **Dart Source:** `compositing.dart:81-83`
  /// **Original:** `@Native<Void Function(Pointer<Void>)>(symbol: 'Scene::dispose')`
  ///
  /// After calling this, the scene is no longer usable.
  void Dispose();

  /// Whether Dispose() has been called.
  bool IsDisposed() const;

  /// Returns an opaque pointer to the internal shared_ptr<Layer>.
  ///
  /// Used by the rendering pipeline to extract the root layer for painting.
  /// Returns nullptr if disposed.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetRootLayerPtr() const;

  ~SceneBridge();

 private:
  SceneBridge(const SceneBridge&) = delete;
  SceneBridge& operator=(const SceneBridge&) = delete;

  // Pimpl - holds the actual Flutter types (shared_ptr<Layer>)
  SceneImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainSceneBridge(
    flutter::swift_bridge::SceneBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseSceneBridge(
    flutter::swift_bridge::SceneBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_SCENE_BRIDGE_H_
