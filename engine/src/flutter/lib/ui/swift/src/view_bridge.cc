// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/view_bridge.h"

#include "include/scene_bridge.h"
#include "include/semantics_update_bridge.h"
#include "include/swift_bridge_engine_registry.h"

// Flutter engine headers (only in .cc file, not exposed to Swift)
#include "flutter/flow/layers/layer.h"
#include "flutter/flow/layers/layer_tree.h"

#include <memory>

namespace flutter::swift_bridge {

__attribute__((used, visibility("default")))
void RenderView(int64_t view_id,
                SceneBridge* scene_bridge,
                double width,
                double height) {
  if (scene_bridge == nullptr) {
    return;
  }

  if (scene_bridge->IsDisposed()) {
    return;
  }

  // Get the render callback registered by the engine during shell startup.
  const auto& render_callback =
      SwiftBridgeEngineRegistry::GetRenderCallback();
  if (!render_callback) {
    return;
  }

  // Extract the root layer from the scene and build a LayerTree.
  const void* root_layer_ptr = scene_bridge->GetRootLayerPtr();
  if (root_layer_ptr == nullptr) {
    return;
  }

  const auto& root_layer =
      *static_cast<const std::shared_ptr<flutter::Layer>*>(root_layer_ptr);
  if (!root_layer) {
    return;
  }

  auto layer_tree = std::make_unique<flutter::LayerTree>(
      root_layer,
      flutter::DlISize(static_cast<int32_t>(width),
                        static_cast<int32_t>(height)));

  float device_pixel_ratio = SwiftBridgeEngineRegistry::GetDevicePixelRatio();

  // Mark that a frame was rendered and submit the layer tree to the engine.
  SwiftBridgeEngineRegistry::SetFrameRendered();
  render_callback(view_id, std::move(layer_tree), device_pixel_ratio);
}

__attribute__((used, visibility("default")))
void UpdateViewSemantics(int64_t view_id,
                         SemanticsUpdateBridge* update_bridge) {
  // STUB IMPLEMENTATION
  //
  // **Dart Source:** `window.dart:398-401`
  // **Original:** `PlatformConfigurationNativeApi::UpdateSemantics(view_id, update)`
  //
  // The Dart version calls:
  //   UIDartState::ThrowIfUIOperationsProhibited();
  //   UIDartState::Current()->platform_configuration()->client()->UpdateSemantics(
  //       view_id, update);
  //
  // For the Swift bridge, we cannot use UIDartState (Dart VM specific).
  // Full implementation will require:
  // 1. A mechanism to access the RuntimeController/PlatformConfigurationClient
  // 2. Extracting the SemanticsNodeUpdates and CustomAccessibilityActionUpdates
  //    from SemanticsUpdateBridge
  // 3. Calling client->UpdateSemantics(view_id, ...)
  //
  // For now, this is a no-op stub that validates inputs.

  if (update_bridge == nullptr) {
    return;
  }

  // TODO: Implement actual semantics update when engine integration is complete.
  // The semantics data is available via:
  // - update_bridge->GetNodesPtr()
  // - update_bridge->GetActionsPtr()
  (void)view_id;
}

}  // namespace flutter::swift_bridge
