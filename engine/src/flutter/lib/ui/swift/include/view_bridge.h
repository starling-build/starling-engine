// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_VIEW_BRIDGE_H_
#define FLUTTER_SWIFT_VIEW_BRIDGE_H_

#include <cstdint>

// Forward declarations - avoid including Flutter engine headers
namespace flutter::swift_bridge {

class SceneBridge;
class SemanticsUpdateBridge;

/// Renders a scene to a view.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/window.dart:383-386`
/// **Original:**
/// ```dart
/// @Native<Void Function(Int64, Pointer<Void>, Double, Double)>(
///   symbol: 'PlatformConfigurationNativeApi::Render',
/// )
/// external static void _render(int viewId, _NativeScene scene, double width, double height);
/// ```
///
/// This function must be called within the scope of the
/// PlatformDispatcher.onBeginFrame or PlatformDispatcher.onDrawFrame
/// callbacks being invoked.
///
/// Extracts the root layer from the SceneBridge, builds a LayerTree, and
/// submits it to the engine via RuntimeDelegate::Render(). The engine
/// reference is obtained from SwiftBridgeEngineRegistry (a global singleton
/// set during engine startup).
///
/// Requires SwiftBridgeEngineRegistry::SetRenderCallback() to have been
/// called prior to use. If no callback is registered, this is a no-op.
///
/// @param view_id The ID of the view to render to.
/// @param scene_bridge The SceneBridge wrapping the scene to render.
/// @param width The width of the render target in physical pixels.
/// @param height The height of the render target in physical pixels.
void RenderView(int64_t view_id,
                SceneBridge* scene_bridge,
                double width,
                double height);

/// Updates semantics data for a view.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/window.dart:398-401`
/// **Original:**
/// ```dart
/// @Native<Void Function(Int64, Pointer<Void>)>(
///   symbol: 'PlatformConfigurationNativeApi::UpdateSemantics',
/// )
/// external static void _updateSemantics(int viewId, _NativeSemanticsUpdate update);
/// ```
///
/// PlatformDispatcher.setSemanticsTreeEnabled must be called with true
/// before sending update through this method.
///
/// **Status:** STUB - Does not perform actual semantics update yet.
/// REASON: Full engine integration (UIDartState, PlatformConfiguration,
/// RuntimeController) not yet connected in Swift bridge layer.
/// When engine integration is complete, this will call through to
/// the platform configuration client's UpdateSemantics method.
///
/// @param view_id The ID of the view to update semantics for.
/// @param update_bridge The SemanticsUpdateBridge containing the semantics data.
void UpdateViewSemantics(int64_t view_id,
                         SemanticsUpdateBridge* update_bridge);

}  // namespace flutter::swift_bridge

#endif  // FLUTTER_SWIFT_VIEW_BRIDGE_H_
