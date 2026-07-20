// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/image_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/image/dl_image.h"

// Skia headers for image encoding
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPixmap.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/encode/SkPngEncoder.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace flutter::swift_bridge {

// Color space enum matching Dart's ColorSpace and C++ flutter::ColorSpace
// Dart Source: painting.dart:1770-1803
// C++ Source: flutter/lib/ui/painting/image.h:16-19
enum ColorSpaceValue {
  kSRGB = 0,
  kExtendedSRGB = 1,
};

// Pimpl implementation holding actual Flutter types
struct ImageBridgeImpl {
  sk_sp<flutter::DlImage> image;
  bool disposed;

  ImageBridgeImpl() : image(nullptr), disposed(false) {}
};

ImageBridge::ImageBridge() {
  impl_ = new ImageBridgeImpl();
}

ImageBridge::~ImageBridge() {
  delete impl_;
}

int ImageBridge::Width() const {
  // Match C++ CanvasImage::width():
  //   return image_ ? image_->width() : 0;
  if (impl_->image) {
    return impl_->image->width();
  }
  return 0;
}

int ImageBridge::Height() const {
  // Match C++ CanvasImage::height():
  //   return image_ ? image_->height() : 0;
  if (impl_->image) {
    return impl_->image->height();
  }
  return 0;
}

int ImageBridge::GetColorSpace() const {
  // Skia-only: all images are sRGB.
  return ColorSpaceValue::kSRGB;
}

void ImageBridge::Dispose() {
  // Match C++ CanvasImage::dispose() from image.cc:42-45
  //   image_.reset();
  //   ClearDartWrapper();  // Not needed in Swift (no Dart VM)
  impl_->image.reset();
  impl_->disposed = true;
}

bool ImageBridge::IsDisposed() const {
  return impl_->disposed;
}

const void* ImageBridge::GetDlImagePtr() const {
  if (!impl_->image) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->image);
}

void ImageBridge::SetDlImageFromPtr(void* dl_image_ptr) {
  // Takes ownership of the heap-allocated sk_sp<DlImage>.
  auto* dl_image = static_cast<sk_sp<flutter::DlImage>*>(dl_image_ptr);
  impl_->image = std::move(*dl_image);
  impl_->disposed = false;
  delete dl_image;
}

void ImageBridge::ToString(char* buffer, int buffer_size) const {
  // Match Dart _Image.toString(): '[$width\u00D7$height]'
  // \u00D7 is the multiplication sign ×
  int w = Width();
  int h = Height();
  snprintf(buffer, buffer_size, "[%d\xC3\x97%d]", w, h);
}

namespace {

// Image byte format enum matching Dart's ImageByteFormat.
// Must be kept in sync with painting.dart:1829-1893.
enum ImageByteFormatValue {
  kRawRGBA = 0,
  kRawStraightRGBA = 1,
  kRawUnmodified = 2,
  kRawExtendedRgba128 = 3,
  kPNG = 4,
};

// Copies pixel data from a raster image, performing color type / alpha type
// conversion if needed. Replicates the logic from image_encoding.cc's
// CopyImageByteData() without depending on //flutter/lib/ui/.
sk_sp<SkData> CopyImageByteData(const sk_sp<SkImage>& raster_image,
                                SkColorType color_type,
                                SkAlphaType alpha_type) {
  if (!raster_image) {
    return nullptr;
  }

  SkPixmap pixmap;
  if (!raster_image->peekPixels(&pixmap)) {
    return nullptr;
  }

  // No conversion needed if formats already match.
  if (pixmap.colorType() == color_type && pixmap.alphaType() == alpha_type) {
    return SkData::MakeWithCopy(pixmap.addr(), pixmap.computeByteSize());
  }

  // Perform pixel format conversion via an intermediate raster surface.
  auto surface = SkSurfaces::Raster(
      SkImageInfo::Make(raster_image->width(), raster_image->height(),
                        color_type, alpha_type, nullptr));
  if (!surface) {
    return nullptr;
  }

  surface->writePixels(pixmap, 0, 0);

  SkPixmap converted_pixmap;
  if (!surface->peekPixels(&converted_pixmap)) {
    return nullptr;
  }

  return SkData::MakeWithCopy(converted_pixmap.addr(),
                              converted_pixmap.computeByteSize());
}

// Encodes a raster SkImage in the given format. Returns the encoded data.
// Replicates the logic from image_encoding.cc's EncodeImage(sk_sp<SkImage>,
// ImageByteFormat) without depending on //flutter/lib/ui/.
sk_sp<SkData> EncodeRasterImage(const sk_sp<SkImage>& raster_image,
                                int format) {
  if (!raster_image) {
    return nullptr;
  }

  switch (format) {
    case kPNG: {
      return SkPngEncoder::Encode(nullptr, raster_image.get(), {});
    }
    case kRawRGBA:
      return CopyImageByteData(raster_image, kRGBA_8888_SkColorType,
                               kPremul_SkAlphaType);
    case kRawStraightRGBA:
      return CopyImageByteData(raster_image, kRGBA_8888_SkColorType,
                               kUnpremul_SkAlphaType);
    case kRawUnmodified:
      return CopyImageByteData(raster_image, raster_image->colorType(),
                               raster_image->alphaType());
    case kRawExtendedRgba128:
      return CopyImageByteData(raster_image, kRGBA_F32_SkColorType,
                               kUnpremul_SkAlphaType);
    default:
      return nullptr;
  }
}

// Persistent error messages returned via out_error pointer.
// These are string literals with static storage duration, so they remain
// valid after the function returns.
static const char* kErrorNoImage = "Image has no underlying data.";
static const char* kErrorNoSkiaImage =
    "Image is GPU-backed and cannot be encoded synchronously. "
    "Async encoding via engine integration is required.";
static const char* kErrorEncodingFailed = "Failed to encode image data.";
static const char* kErrorInvalidFormat = "Invalid image byte format.";
static const char* kErrorMakeRaster = "Failed to create raster copy of image.";

}  // namespace

bool ImageBridge::ToByteData(int format,
                             const void** out_data,
                             int64_t* out_length,
                             const char** out_error) const {
  if (!impl_->image) {
    *out_error = kErrorNoImage;
    return false;
  }

  if (format < 0 || format > 4) {
    *out_error = kErrorInvalidFormat;
    return false;
  }

  // Get the Skia image from the DlImage.
  sk_sp<SkImage> skia_image = impl_->image->skia_image();
  if (!skia_image) {
    // Image is GPU-only (Impeller texture without Skia backing).
    // The Dart version handles this via async GPU readback through task runners.
    *out_error = kErrorNoSkiaImage;
    return false;
  }

  // If the image is not raster (e.g., GPU texture), convert to raster first.
  sk_sp<SkImage> raster_image = skia_image;
  if (!skia_image->peekPixels(nullptr)) {
    raster_image = skia_image->makeRasterImage();
    if (!raster_image) {
      *out_error = kErrorMakeRaster;
      return false;
    }
  }

  // Encode the raster image.
  sk_sp<SkData> encoded = EncodeRasterImage(raster_image, format);
  if (!encoded || encoded->isEmpty()) {
    *out_error = kErrorEncodingFailed;
    return false;
  }

  // Copy the encoded data into a malloc'd buffer that the caller owns.
  // The caller must call FreeByteData() to release it.
  size_t size = encoded->size();
  void* copy = malloc(size);
  if (!copy) {
    *out_error = kErrorEncodingFailed;
    return false;
  }
  memcpy(copy, encoded->data(), size);

  *out_data = copy;
  *out_length = static_cast<int64_t>(size);
  return true;
}

void ImageBridge::FreeByteData(const void* data) {
  free(const_cast<void*>(data));
}

}  // namespace flutter::swift_bridge
