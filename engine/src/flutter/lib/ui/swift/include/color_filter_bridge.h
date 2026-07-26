// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_COLOR_FILTER_BRIDGE_H_
#define FLUTTER_SWIFT_COLOR_FILTER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ColorFilterBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainColorFilterBridge(
    flutter::swift_bridge::ColorFilterBridge* p) noexcept;
void ReleaseColorFilterBridge(
    flutter::swift_bridge::ColorFilterBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct ColorFilterImpl;

/// C++ bridge wrapping Flutter's ColorFilter native class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:4135-4181`
/// **Original Name:** `_ColorFilter`
///
/// This is the native backing for the public ColorFilter class.
/// It wraps a DlColorFilter via pimpl pattern. Supports four filter types:
/// - Mode (blend a color with a blend mode)
/// - Matrix (5x4 color transformation matrix)
/// - LinearToSrgbGamma
/// - SrgbToLinearGamma
///
/// Architecture:
/// - Swift _NativeColorFilter class wraps ColorFilterBridge
/// - ColorFilterBridge holds DlColorFilter via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainColorFilterBridge, ReleaseColorFilterBridge)
        ColorFilterBridge
    : public IntrusiveReferenceCounted<ColorFilterBridge> {
 public:
  /// Creates a ColorFilterBridge.
  ///
  /// **Dart Source:** `painting.dart:4167-4168`
  /// **Original:** `_constructor()` (@Native ColorFilter::Create)
  ColorFilterBridge();

  /// Initialize as a mode (blend) color filter.
  ///
  /// **Dart Source:** `painting.dart:4170-4171`
  /// **Original:** `_initMode(int color, int blendMode)`
  ///
  /// @param color ARGB color as a 32-bit integer
  /// @param blend_mode BlendMode index
  void InitMode(int color, int blend_mode);

  /// Initialize as a matrix color filter.
  ///
  /// **Dart Source:** `painting.dart:4173-4174`
  /// **Original:** `_initMatrix(Float32List matrix)`
  ///
  /// @param matrix Pointer to 20 float values (5x4 color matrix)
  void InitMatrix(const float* matrix);

  /// Initialize as a linear-to-sRGB gamma color filter.
  ///
  /// **Dart Source:** `painting.dart:4176-4177`
  /// **Original:** `_initLinearToSrgbGamma()`
  void InitLinearToSrgbGamma();

  /// Initialize as an sRGB-to-linear gamma color filter.
  ///
  /// **Dart Source:** `painting.dart:4179-4180`
  /// **Original:** `_initSrgbToLinearGamma()`
  void InitSrgbToLinearGamma();

  /// Returns an opaque pointer to the underlying DlColorFilter.
  ///
  /// The returned pointer is a `const shared_ptr<const DlColorFilter>*`
  /// pointing to the internal storage. The caller must NOT delete this pointer.
  /// The pointer is valid as long as this ColorFilterBridge is alive.
  ///
  /// Returns nullptr if no filter has been initialized.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetFilterPtr() const;

  ~ColorFilterBridge();

 private:
  ColorFilterBridge(const ColorFilterBridge&) = delete;
  ColorFilterBridge& operator=(const ColorFilterBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlColorFilter)
  ColorFilterImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainColorFilterBridge(
    flutter::swift_bridge::ColorFilterBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseColorFilterBridge(
    flutter::swift_bridge::ColorFilterBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_COLOR_FILTER_BRIDGE_H_
