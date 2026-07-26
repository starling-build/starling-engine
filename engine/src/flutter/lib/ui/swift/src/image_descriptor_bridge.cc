// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/image_descriptor_bridge.h"
#include "include/codec_bridge.h"
#include "include/immutable_buffer_bridge.h"

// Skia headers
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkImageInfo.h"

#include <cstdio>
#include <memory>
#include <optional>

namespace flutter::swift_bridge {

// Pimpl implementation holding image metadata and buffer data.
//
// Mirrors the C++ flutter::ImageDescriptor class's internal state,
// but without directly holding an ImageGenerator (which is in lib/ui).
// Instead, codec creation is delegated to CodecBridge factory methods.
struct ImageDescriptorBridgeImpl {
  sk_sp<SkData> buffer;
  SkImageInfo image_info;
  std::optional<size_t> row_bytes;
  bool is_encoded;  // true for encoded data, false for raw pixels
  bool disposed;

  // Constructor for raw pixel data
  ImageDescriptorBridgeImpl(sk_sp<SkData> buf,
                            SkImageInfo info,
                            std::optional<size_t> rb)
      : buffer(std::move(buf)),
        image_info(std::move(info)),
        row_bytes(rb),
        is_encoded(false),
        disposed(false) {}

  // Constructor for encoded data
  ImageDescriptorBridgeImpl(sk_sp<SkData> buf, SkImageInfo info)
      : buffer(std::move(buf)),
        image_info(std::move(info)),
        row_bytes(std::nullopt),
        is_encoded(true),
        disposed(false) {}
};

// Raw pixel data constructor
// Mirrors ImageDescriptor::initRaw (image_descriptor.cc:81-108)
ImageDescriptorBridge::ImageDescriptorBridge(ImmutableBufferBridge* buffer,
                                             int width,
                                             int height,
                                             int row_bytes,
                                             int pixel_format) {
  // Get the sk_sp<SkData> from the ImmutableBufferBridge
  sk_sp<SkData> data;
  if (buffer) {
    const void* sk_data_ptr = buffer->GetSkDataPtr();
    if (sk_data_ptr) {
      data = *static_cast<const sk_sp<SkData>*>(sk_data_ptr);
    }
  }

  // Determine color type from pixel format
  // Mirrors ImageDescriptor::initRaw (image_descriptor.cc:87-101)
  SkColorType color_type = kUnknown_SkColorType;
  SkAlphaType alpha_type = kPremul_SkAlphaType;
  switch (pixel_format) {
    case 0:  // PixelFormat.rgba8888
      color_type = kRGBA_8888_SkColorType;
      break;
    case 1:  // PixelFormat.bgra8888
      color_type = kBGRA_8888_SkColorType;
      break;
    case 2:  // PixelFormat.rgbaFloat32
      color_type = kRGBA_F32_SkColorType;
      alpha_type = kUnpremul_SkAlphaType;
      break;
  }

  auto image_info = SkImageInfo::Make(width, height, color_type, alpha_type);
  impl_ = new ImageDescriptorBridgeImpl(
      std::move(data), std::move(image_info),
      row_bytes == -1 ? std::nullopt : std::optional<size_t>(row_bytes));
}

// Encoded data constructor
// Mirrors ImageDescriptor::initEncoded (image_descriptor.cc:40-79)
ImageDescriptorBridge::ImageDescriptorBridge(ImmutableBufferBridge* buffer)
    : impl_(nullptr) {
  if (!buffer || buffer->IsDisposed()) {
    return;
  }

  const void* sk_data_ptr = buffer->GetSkDataPtr();
  if (!sk_data_ptr) {
    return;
  }
  sk_sp<SkData> data = *static_cast<const sk_sp<SkData>*>(sk_data_ptr);
  if (!data) {
    return;
  }

  // Validate the encoded data and extract image dimensions by creating
  // a temporary codec and decoding one frame.
  //
  // DIFFERENCE FROM DART: The Dart version uses UIDartState to access
  // ImageGeneratorRegistry for decoder discovery and dimension reading.
  // We use CodecBridge::CreateFromEncodedData which uses Skia's built-in
  // codec (supports PNG, JPEG, GIF, WebP, BMP, ICO, WBMP).
  // REASON: No Dart VM; UIDartState is unavailable.
  auto* codec = CodecBridge::CreateFromEncodedData(sk_data_ptr);
  if (!codec) {
    // No compatible decoder found - IsValid() will return false
    return;
  }

  // Decode one frame to get image dimensions
  int w = 0;
  int h = 0;
  if (codec->DecodeNextFrame()) {
    auto* img = codec->GetLastFrameImage();
    if (img) {
      w = img->Width();
      h = img->Height();
      img->Release();
    }
  }
  codec->Release();

  auto image_info = SkImageInfo::Make(w, h, kN32_SkColorType,
                                      kPremul_SkAlphaType);
  impl_ = new ImageDescriptorBridgeImpl(std::move(data), std::move(image_info));
}

bool ImageDescriptorBridge::IsValid() const {
  return impl_ != nullptr && !impl_->disposed;
}

ImageDescriptorBridge::~ImageDescriptorBridge() {
  delete impl_;
}

int ImageDescriptorBridge::GetWidth() const {
  if (!impl_ || impl_->disposed) {
    return 0;
  }
  return impl_->image_info.width();
}

int ImageDescriptorBridge::GetHeight() const {
  if (!impl_ || impl_->disposed) {
    return 0;
  }
  return impl_->image_info.height();
}

int ImageDescriptorBridge::GetBytesPerPixel() const {
  if (!impl_ || impl_->disposed) {
    return 0;
  }
  return impl_->image_info.bytesPerPixel();
}

CodecBridge* ImageDescriptorBridge::InstantiateCodec(
    int target_width,
    int target_height) const {
  if (!impl_ || impl_->disposed || !impl_->buffer) {
    return nullptr;
  }

  const void* sk_data_ptr = &impl_->buffer;

  if (impl_->is_encoded) {
    // For encoded data, delegate to CodecBridge::CreateFromEncodedData
    return CodecBridge::CreateFromEncodedData(sk_data_ptr);
  }

  // For raw pixel data, delegate to CodecBridge::CreateFromRawPixels
  size_t rb = impl_->row_bytes.value_or(
      static_cast<size_t>(impl_->image_info.width() *
                          impl_->image_info.bytesPerPixel()));

  return CodecBridge::CreateFromRawPixels(
      sk_data_ptr, impl_->image_info.width(), impl_->image_info.height(),
      static_cast<int>(rb),
      static_cast<int>(impl_->image_info.colorType()),
      static_cast<int>(impl_->image_info.alphaType()));
}

void ImageDescriptorBridge::Dispose() {
  if (impl_) {
    impl_->disposed = true;
    impl_->buffer.reset();
  }
}

void ImageDescriptorBridge::ToString(char* buffer, int buffer_size) const {
  if (!impl_ || buffer_size <= 0) {
    if (buffer_size > 0) {
      buffer[0] = '\0';
    }
    return;
  }

  // Mirrors: 'ImageDescriptor(width: ${_width ?? '?'}, height: ${_height ??
  // '?'}, bytes per pixel: ${_bytesPerPixel ?? '?'})'
  if (impl_->disposed) {
    std::snprintf(buffer, buffer_size,
                  "ImageDescriptor(width: ?, height: ?, bytes per pixel: ?)");
  } else {
    std::snprintf(
        buffer, buffer_size,
        "ImageDescriptor(width: %d, height: %d, bytes per pixel: %d)",
        impl_->image_info.width(), impl_->image_info.height(),
        impl_->image_info.bytesPerPixel());
  }
}

}  // namespace flutter::swift_bridge
