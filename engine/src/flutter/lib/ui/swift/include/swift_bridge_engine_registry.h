// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_BRIDGE_ENGINE_REGISTRY_H_
#define FLUTTER_SWIFT_BRIDGE_ENGINE_REGISTRY_H_

#include "swift_bridge_export.h"

#include <cstdint>
#include <functional>
#include <memory>

// Forward declarations - the bridge layer never includes Flutter engine
// headers directly. The LayerTree type is forward-declared here and the
// full header is only included in .cc files.
namespace flutter {
class LayerTree;
}

namespace flutter::swift_bridge {

/// A simple global registry that stores callbacks to engine functions
/// for use by Swift bridge free functions.
///
/// Bridge functions like RenderView() and ScheduleFrame() need access to the
/// engine but are free functions with no object context. This registry provides
/// that connection via a global singleton with typed callbacks.
///
/// **Lifecycle:**
///   - Set*() methods are called by the shell/engine during startup
///   - Get*() / callbacks are invoked by bridge functions (RenderView, etc.)
///   - Set*(nullptr) is called during engine teardown
///
/// **Thread safety:** The callbacks are set once during engine startup
/// and cleared during engine teardown. Bridge functions are called on the UI
/// thread. No concurrent mutation is expected.
///
/// **Phase 1 design:** This is intentionally simple. A more sophisticated
/// approach (e.g., passing engine references through PlatformDispatcherBridge)
/// can replace this later. The callback-based approach avoids requiring the
/// bridge shared_library to depend on //flutter/runtime (which transitively
/// depends on //flutter/lib/ui and Dart VM).
class FLUTTER_SWIFT_BRIDGE_EXPORT SwiftBridgeEngineRegistry {
 public:
  /// Callback type for RuntimeDelegate::Render().
  ///
  /// Signature matches RuntimeDelegate::Render():
  ///   void Render(int64_t view_id,
  ///               std::unique_ptr<flutter::LayerTree> layer_tree,
  ///               float device_pixel_ratio)
  using RenderCallback =
      std::function<void(int64_t view_id,
                         std::unique_ptr<flutter::LayerTree> layer_tree,
                         float device_pixel_ratio)>;

  /// Callback type for RuntimeDelegate::ScheduleFrame().
  ///
  /// Signature matches RuntimeDelegate::ScheduleFrame():
  ///   void ScheduleFrame(bool regenerate_layer_trees)
  using ScheduleFrameCallback =
      std::function<void(bool regenerate_layer_trees)>;

  /// Sets the render callback that RenderView() should use.
  ///
  /// Typically bound to RuntimeDelegate::Render() (i.e., Engine::Render()).
  ///
  /// @param callback The render callback, or nullptr to clear.
  static void SetRenderCallback(RenderCallback callback);

  /// Returns the currently registered render callback.
  /// Returns nullptr (empty function) if none is registered.
  static const RenderCallback& GetRenderCallback();

  /// Sets the schedule-frame callback that ScheduleFrame() should use.
  ///
  /// Typically bound to RuntimeDelegate::ScheduleFrame().
  ///
  /// @param callback The schedule-frame callback, or nullptr to clear.
  static void SetScheduleFrameCallback(ScheduleFrameCallback callback);

  /// Returns the currently registered schedule-frame callback.
  /// Returns nullptr (empty function) if none is registered.
  static const ScheduleFrameCallback& GetScheduleFrameCallback();

  /// Sets the default device pixel ratio for rendering.
  ///
  /// In the Dart path, the device pixel ratio comes from ViewportMetrics
  /// per-view. For Phase 1, we use a single global value that the shell
  /// sets during view configuration.
  ///
  /// @param dpr The device pixel ratio (e.g., 2.0 for Retina displays).
  static void SetDevicePixelRatio(float dpr);

  /// Returns the current device pixel ratio (default: 1.0).
  static float GetDevicePixelRatio();

  /// Returns true if a frame has been rendered since the last reset.
  ///
  /// Used by the blue_screen_demo to verify the rendering pipeline completed.
  static bool FrameWasRendered();

  /// Sets the frame-rendered flag to true.
  ///
  /// Called by RenderView() when a frame is submitted to the engine.
  static void SetFrameRendered();

  /// Resets the frame-rendered flag to false.
  static void ResetFrameRendered();

  /// Sets the font collection for text rendering.
  ///
  /// The pointer must point to a std::shared_ptr<txt::FontCollection>.
  /// The registry takes a copy of the shared_ptr (i.e., increments the
  /// reference count).
  ///
  /// @param font_collection_ptr Pointer to std::shared_ptr<txt::FontCollection>,
  ///   or nullptr to clear.
  static void SetFontCollection(void* font_collection_ptr);

  /// Returns opaque pointer to the stored std::shared_ptr<txt::FontCollection>.
  /// Returns nullptr if no font collection is set.
  static void* GetFontCollection();

  /// Clears all registered callbacks and resets device pixel ratio.
  ///
  /// Should be called during engine teardown.
  static void Reset();

 private:
  SwiftBridgeEngineRegistry() = delete;
  ~SwiftBridgeEngineRegistry() = delete;

  static RenderCallback render_callback_;
  static ScheduleFrameCallback schedule_frame_callback_;
  static float device_pixel_ratio_;
  static bool frame_rendered_;
  // Opaque storage for std::shared_ptr<txt::FontCollection>.
  // We store it as raw bytes to avoid including txt headers in this header.
  static void* font_collection_storage_;
};

}  // namespace flutter::swift_bridge

#endif  // FLUTTER_SWIFT_BRIDGE_ENGINE_REGISTRY_H_
