// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_IMAGE_BRIDGE_H_
#define FLUTTER_SWIFT_IMAGE_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ImageBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainImageBridge(flutter::swift_bridge::ImageBridge* p) noexcept;
void ReleaseImageBridge(flutter::swift_bridge::ImageBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct ImageBridgeImpl;

/// C++ bridge wrapping Flutter's CanvasImage / DlImage.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:2175-2233`
/// **Original:** `_Image` class (internal native wrapper for Image)
///
/// This bridge corresponds to the Dart `_Image` class, which is the native
/// wrapper around the C++ `CanvasImage`. The Swift `Image` class manages
/// handle counting on top of this bridge (mirroring how Dart's `Image` class
/// manages handles on top of `_Image`).
///
/// Architecture:
/// - Swift Image class wraps ImageBridge (like Dart Image wraps _Image)
/// - ImageBridge wraps the underlying DlImage (display list image)
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - Multiple Swift Image handles can share the same ImageBridge
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainImageBridge, ReleaseImageBridge)
        ImageBridge
    : public IntrusiveReferenceCounted<ImageBridge> {
 public:
  /// Creates an ImageBridge (empty, image must be set later by the engine).
  ///
  /// **Dart Source:** `painting.dart:2182-2183`
  /// **Original:** `_Image._()`
  ImageBridge();

  /// The number of image pixels along the image's horizontal axis.
  ///
  /// **Dart Source:** `painting.dart:2185-2186`
  /// **Original:** `@Native<Int32 Function(Pointer<Void>)>(symbol: 'Image::width')`
  int Width() const;

  /// The number of image pixels along the image's vertical axis.
  ///
  /// **Dart Source:** `painting.dart:2188-2189`
  /// **Original:** `@Native<Int32 Function(Pointer<Void>)>(symbol: 'Image::height')`
  int Height() const;

  /// Returns the color space as an integer (0 = sRGB, 1 = extendedSRGB).
  ///
  /// **Dart Source:** `painting.dart:2228-2229`
  /// **Original:** `@Native<Int32 Function(Pointer<Void>)>(symbol: 'Image::colorSpace')`
  int GetColorSpace() const;

  /// Release the underlying image resources.
  ///
  /// **Dart Source:** `painting.dart:2208-2224`
  /// **Original:** `void dispose()` + `@Native void _dispose()`
  ///
  /// After calling this method, width/height will return 0
  /// and the underlying image data is released.
  void Dispose();

  /// Whether dispose() has been called on the underlying image.
  bool IsDisposed() const;

  /// Returns a string representation of this image (e.g., "[640×480]").
  ///
  /// **Dart Source:** `painting.dart:2231-2232`
  /// **Original:** `String toString() => '[$width\u00D7$height]';`
  void ToString(char* buffer, int buffer_size) const;

  /// Encodes the image to the specified byte format synchronously.
  ///
  /// **Dart Source:** `painting.dart:2191-2205`
  /// **Original:** `Future<ByteData?> toByteData({ImageByteFormat format})`
  ///              `@Native String? _toByteData(int format, callback)`
  ///
  /// DIFFERENCE FROM DART: Synchronous instead of async. Dart version uses
  /// task runners (IO/raster/UI) for GPU readback before encoding. This
  /// version only works for raster-backed (CPU) images. GPU-only images
  /// will return false.
  /// REASON: The async path requires UIDartState for task runner access,
  /// which is Dart VM specific. A future engine integration layer will
  /// provide the async path.
  ///
  /// Format values (must match ImageByteFormat enum):
  ///   0 = rawRgba (premultiplied RGBA8888)
  ///   1 = rawStraightRgba (unpremultiplied RGBA8888)
  ///   2 = rawUnmodified (native pixel format)
  ///   3 = rawExtendedRgba128 (unpremultiplied RGBA Float32)
  ///   4 = png
  ///
  /// On success: returns true, sets out_data and out_length.
  ///             Caller must call FreeByteData() to release out_data.
  /// On failure: returns false, sets out_error to error message.
  bool ToByteData(int format,
                  const void** out_data,
                  int64_t* out_length,
                  const char** out_error) const;

  /// Frees byte data allocated by ToByteData.
  static void FreeByteData(const void* data);

  /// Returns an opaque pointer to the underlying sk_sp<const DlImage>.
  ///
  /// This is used by other bridge classes (e.g., ImageShaderBridge) that need
  /// access to the underlying DlImage. Returns nullptr if no image is set.
  ///
  /// The returned pointer is valid only for the lifetime of this ImageBridge.
  const void* GetDlImagePtr() const;

  /// Sets the underlying DlImage from an opaque pointer.
  ///
  /// Takes ownership of the heap-allocated sk_sp<DlImage> pointed to by
  /// `dl_image_ptr`. The pointer is consumed (moved from and deleted).
  ///
  /// This is used by CodecBridge to set the decoded frame image.
  void SetDlImageFromPtr(void* dl_image_ptr);

  ~ImageBridge();

 private:
  ImageBridge(const ImageBridge&) = delete;
  ImageBridge& operator=(const ImageBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlImage)
  ImageBridgeImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainImageBridge(
    flutter::swift_bridge::ImageBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseImageBridge(
    flutter::swift_bridge::ImageBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_IMAGE_BRIDGE_H_
