// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_GRADIENT_BRIDGE_H_
#define FLUTTER_SWIFT_GRADIENT_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class GradientBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainGradientBridge(flutter::swift_bridge::GradientBridge* p) noexcept;
void ReleaseGradientBridge(flutter::swift_bridge::GradientBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct GradientImpl;

/// C++ bridge wrapping Flutter's Gradient shader class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:4808-5113`
///
/// Gradient is a shader that renders color gradients. It supports three types:
/// - Linear gradients (from point A to point B)
/// - Radial gradients (from center outward, optionally with focal point)
/// - Sweep gradients (angular, around a center point)
///
/// Architecture:
/// - Swift Gradient class wraps GradientBridge
/// - GradientBridge holds DlColorSource via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainGradientBridge, ReleaseGradientBridge)
        GradientBridge
    : public IntrusiveReferenceCounted<GradientBridge> {
 public:
  /// Creates a GradientBridge.
  GradientBridge();

  /// Initialize as a linear gradient.
  ///
  /// **Dart Source:** `painting.dart:4844-4863`
  /// **Original:** `Gradient.linear(...)`
  ///
  /// @param from_x X coordinate of start point
  /// @param from_y Y coordinate of start point
  /// @param to_x X coordinate of end point
  /// @param to_y Y coordinate of end point
  /// @param colors Wide color buffer (ARGB as floats, 4 values per color)
  /// @param color_count Number of colors
  /// @param color_stops Stop values (0.0 to 1.0), or nullptr for uniform distribution
  /// @param stop_count Number of stops (must match color_count if provided)
  /// @param tile_mode Tile mode index (0=clamp, 1=repeated, 2=mirror, 3=decal)
  /// @param matrix4 Optional 4x4 transform matrix (16 doubles), or nullptr
  void InitLinear(double from_x,
                  double from_y,
                  double to_x,
                  double to_y,
                  const float* colors,
                  int color_count,
                  const float* color_stops,
                  int stop_count,
                  int tile_mode,
                  const double* matrix4);

  /// Initialize as a radial gradient.
  ///
  /// **Dart Source:** `painting.dart:4899-4948`
  /// **Original:** `Gradient.radial(...)`
  ///
  /// @param center_x X coordinate of center
  /// @param center_y Y coordinate of center
  /// @param radius Radius of the gradient
  /// @param colors Wide color buffer (ARGB as floats, 4 values per color)
  /// @param color_count Number of colors
  /// @param color_stops Stop values (0.0 to 1.0), or nullptr
  /// @param stop_count Number of stops
  /// @param tile_mode Tile mode index
  /// @param matrix4 Optional transform matrix, or nullptr
  void InitRadial(double center_x,
                  double center_y,
                  double radius,
                  const float* colors,
                  int color_count,
                  const float* color_stops,
                  int stop_count,
                  int tile_mode,
                  const double* matrix4);

  /// Initialize as a two-point conical gradient.
  ///
  /// **Dart Source:** `painting.dart:4930-4947`
  /// **Original:** Part of `Gradient.radial(...)` when focal point is provided
  ///
  /// @param focal_x X coordinate of focal point
  /// @param focal_y Y coordinate of focal point
  /// @param focal_radius Radius at focal point
  /// @param center_x X coordinate of center
  /// @param center_y Y coordinate of center
  /// @param radius Radius at center
  /// @param colors Wide color buffer
  /// @param color_count Number of colors
  /// @param color_stops Stop values, or nullptr
  /// @param stop_count Number of stops
  /// @param tile_mode Tile mode index
  /// @param matrix4 Optional transform matrix, or nullptr
  void InitConical(double focal_x,
                   double focal_y,
                   double focal_radius,
                   double center_x,
                   double center_y,
                   double radius,
                   const float* colors,
                   int color_count,
                   const float* color_stops,
                   int stop_count,
                   int tile_mode,
                   const double* matrix4);

  /// Initialize as a sweep gradient.
  ///
  /// **Dart Source:** `painting.dart:5003-5031`
  /// **Original:** `Gradient.sweep(...)`
  ///
  /// @param center_x X coordinate of center
  /// @param center_y Y coordinate of center
  /// @param colors Wide color buffer
  /// @param color_count Number of colors
  /// @param color_stops Stop values, or nullptr
  /// @param stop_count Number of stops
  /// @param tile_mode Tile mode index
  /// @param start_angle Start angle in radians
  /// @param end_angle End angle in radians
  /// @param matrix4 Optional transform matrix, or nullptr
  void InitSweep(double center_x,
                 double center_y,
                 const float* colors,
                 int color_count,
                 const float* color_stops,
                 int stop_count,
                 int tile_mode,
                 double start_angle,
                 double end_angle,
                 const double* matrix4);

  /// Whether dispose() has been called.
  ///
  /// Inherited from ShaderBridge behavior.
  bool DebugDisposed() const;

  /// Release the resources used by this gradient.
  ///
  /// **Dart Source:** `painting.dart:4631-4646`
  void Dispose();

  /// Returns a pointer to the internal shared_ptr<DlColorSource>.
  ///
  /// Used by CanvasBridge to extract the shader for rendering.
  /// Returns nullptr if no shader is set or if disposed.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetShaderPtr() const;

  ~GradientBridge();

 private:
  GradientBridge(const GradientBridge&) = delete;
  GradientBridge& operator=(const GradientBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlColorSource)
  GradientImpl* impl_;
  bool debug_disposed_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainGradientBridge(
    flutter::swift_bridge::GradientBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseGradientBridge(
    flutter::swift_bridge::GradientBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_GRADIENT_BRIDGE_H_
