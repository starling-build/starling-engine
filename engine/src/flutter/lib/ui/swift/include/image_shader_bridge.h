// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_IMAGE_SHADER_BRIDGE_H_
#define FLUTTER_SWIFT_IMAGE_SHADER_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ImageShaderBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainImageShaderBridge(
    flutter::swift_bridge::ImageShaderBridge* p) noexcept;
void ReleaseImageShaderBridge(
    flutter::swift_bridge::ImageShaderBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct ImageShaderImpl;
class ImageBridge;

/// C++ bridge wrapping Flutter's ImageShader class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:5115-5185`
///
/// ImageShader is a shader that tiles an image. It takes an Image, tile modes
/// for the x and y directions, an optional filter quality, and a 4x4
/// transformation matrix.
///
/// Architecture:
/// - Swift ImageShader class wraps ImageShaderBridge
/// - ImageShaderBridge holds DlColorSource (DlImageColorSource) via pimpl
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - The ImageBridge (from Image) provides the underlying DlImage
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainImageShaderBridge, ReleaseImageShaderBridge)
        ImageShaderBridge
    : public IntrusiveReferenceCounted<ImageShaderBridge> {
 public:
  /// Creates an ImageShaderBridge.
  ImageShaderBridge();

  /// Initialize with an image and tiling parameters.
  ///
  /// **Dart Source:** `painting.dart:5137-5158`
  /// **Original:** `ImageShader(...)`
  ///
  /// @param image_bridge The ImageBridge wrapping the source image
  /// @param tmx Tile mode index for x direction (0=clamp, 1=repeated, 2=mirror, 3=decal)
  /// @param tmy Tile mode index for y direction
  /// @param filter_quality_index Filter quality index (-1=unspecified, 0=none, 1=low, 2=medium, 3=high)
  /// @param matrix4 4x4 transformation matrix (16 doubles, column-major)
  /// @returns true if initialization succeeded, false if image is invalid
  bool InitWithImage(const ImageBridge* image_bridge,
                     int tmx,
                     int tmy,
                     int filter_quality_index,
                     const double* matrix4);

  /// Whether dispose() has been called.
  ///
  /// **Dart Source:** `painting.dart:4619-4629` (inherited from Shader)
  bool DebugDisposed() const;

  /// Release the resources used by this image shader.
  ///
  /// **Dart Source:** `painting.dart:5161-5165`
  /// **Original:** `void dispose()`
  void Dispose();

  /// Returns a pointer to the internal shared_ptr<DlColorSource>.
  ///
  /// Used by CanvasBridge to extract the shader for rendering.
  /// Returns nullptr if no shader is set or if disposed.
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetShaderPtr() const;

  ~ImageShaderBridge();

 private:
  ImageShaderBridge(const ImageShaderBridge&) = delete;
  ImageShaderBridge& operator=(const ImageShaderBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlColorSource, DlImage)
  ImageShaderImpl* impl_;
  bool debug_disposed_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainImageShaderBridge(
    flutter::swift_bridge::ImageShaderBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseImageShaderBridge(
    flutter::swift_bridge::ImageShaderBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_IMAGE_SHADER_BRIDGE_H_
