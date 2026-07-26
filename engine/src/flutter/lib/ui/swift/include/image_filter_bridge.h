// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_IMAGE_FILTER_BRIDGE_H_
#define FLUTTER_SWIFT_IMAGE_FILTER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"
#include "color_filter_bridge.h"
#include "fragment_shader_bridge.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ImageFilterBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainImageFilterBridge(
    flutter::swift_bridge::ImageFilterBridge* p) noexcept;
void ReleaseImageFilterBridge(
    flutter::swift_bridge::ImageFilterBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct ImageFilterImpl;

/// C++ bridge wrapping Flutter's _ImageFilter native class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:4511-4607`
/// **Original Name:** `_ImageFilter`
///
/// This is the native backing for the ImageFilter implementations.
/// It wraps a DlImageFilter via pimpl pattern. Supports seven filter types:
/// - Blur (Gaussian blur with sigma and tile mode)
/// - Dilate (morphological dilation with radii)
/// - Erode (morphological erosion with radii)
/// - Matrix (4x4 transformation matrix with filter quality)
/// - ColorFilter (wraps a ColorFilterBridge)
/// - Compose (composes outer and inner filters)
/// - Shader (wraps a FragmentShaderBridge as image filter)
///
/// Architecture:
/// - Swift NativeImageFilter class wraps ImageFilterBridge
/// - ImageFilterBridge holds DlImageFilter via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainImageFilterBridge, ReleaseImageFilterBridge)
        ImageFilterBridge
    : public IntrusiveReferenceCounted<ImageFilterBridge> {
 public:
  /// Creates an ImageFilterBridge.
  ///
  /// **Dart Source:** `painting.dart:4569-4570`
  /// **Original:** `_constructor()` (@Native ImageFilter::Create)
  ImageFilterBridge();

  /// Initialize as a Gaussian blur filter.
  ///
  /// **Dart Source:** `painting.dart:4572-4576`
  /// **Original:** `_initBlur(double sigmaX, double sigmaY, int tileMode)`
  ///
  /// @param sigma_x Horizontal blur sigma
  /// @param sigma_y Vertical blur sigma
  /// @param tile_mode_index TileMode index (-1 for dynamic/unspecified)
  void InitBlur(double sigma_x, double sigma_y, int tile_mode_index);

  /// Initialize as a dilate (morphological dilation) filter.
  ///
  /// **Dart Source:** `painting.dart:4578-4582`
  /// **Original:** `_initDilate(double radiusX, double radiusY)`
  ///
  /// @param radius_x Horizontal dilation radius
  /// @param radius_y Vertical dilation radius
  void InitDilate(double radius_x, double radius_y);

  /// Initialize as an erode (morphological erosion) filter.
  ///
  /// **Dart Source:** `painting.dart:4584-4588`
  /// **Original:** `_initErode(double radiusX, double radiusY)`
  ///
  /// @param radius_x Horizontal erosion radius
  /// @param radius_y Vertical erosion radius
  void InitErode(double radius_x, double radius_y);

  /// Initialize as a matrix transformation filter.
  ///
  /// **Dart Source:** `painting.dart:4590-4591`
  /// **Original:** `_initMatrix(Float64List matrix4, int filterQuality)`
  ///
  /// @param matrix4 Pointer to 16 double values (4x4 column-major matrix)
  /// @param filter_quality_index FilterQuality index
  void InitMatrix(const double* matrix4, int filter_quality_index);

  /// Initialize as a color filter image filter.
  ///
  /// **Dart Source:** `painting.dart:4593-4594`
  /// **Original:** `_initColorFilter(_ColorFilter? colorFilter)`
  ///
  /// @param color_filter The ColorFilterBridge to wrap (may be null)
  void InitColorFilter(ColorFilterBridge* color_filter);

  /// Initialize as a compose filter (outer applied after inner).
  ///
  /// **Dart Source:** `painting.dart:4596-4599`
  /// **Original:** `_initComposed(_ImageFilter outerFilter, _ImageFilter innerFilter)`
  ///
  /// @param outer The outer filter (applied second)
  /// @param inner The inner filter (applied first)
  void InitComposed(ImageFilterBridge* outer, ImageFilterBridge* inner);

  /// Initialize from a fragment shader as an image filter.
  ///
  /// **Dart Source:** `painting.dart:4601-4602`
  /// **Original:** `_initShader(FragmentShader shader)`
  ///
  /// @param shader The FragmentShaderBridge to use as image filter
  void InitShader(FragmentShaderBridge* shader);

  /// Compares two ImageFilterBridges for equality.
  ///
  /// **Dart Source:** `painting.dart:4504-4505`
  /// **Original:** `static bool _equals(_ImageFilter a, _ImageFilter b)`
  ///
  /// @param other The other ImageFilterBridge to compare with
  /// @return true if the underlying DlImageFilters are equal
  bool Equals(const ImageFilterBridge* other) const;

  /// Returns whether this filter has dynamic tile mode (blur with unspecified tile mode).
  ///
  /// When tile mode is dynamic, the actual tile mode is determined at render time.
  bool IsDynamicTileMode() const;

  /// Returns a pointer to the internal shared_ptr<DlImageFilter>.
  ///
  /// Used by CanvasBridge to extract the image filter for rendering.
  /// @param tile_mode_index The tile mode to apply for dynamic-tile-mode filters.
  ///        Ignored for non-dynamic filters.
  /// Returns nullptr if no filter is set.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetFilterPtr(int tile_mode_index) const;

  ~ImageFilterBridge();

 private:
  ImageFilterBridge(const ImageFilterBridge&) = delete;
  ImageFilterBridge& operator=(const ImageFilterBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlImageFilter)
  ImageFilterImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainImageFilterBridge(
    flutter::swift_bridge::ImageFilterBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseImageFilterBridge(
    flutter::swift_bridge::ImageFilterBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_IMAGE_FILTER_BRIDGE_H_
