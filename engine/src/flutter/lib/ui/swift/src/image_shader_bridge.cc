// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/image_shader_bridge.h"
#include "include/image_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/effects/dl_color_source.h"
#include "flutter/display_list/dl_tile_mode.h"
#include "flutter/display_list/image/dl_image.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace flutter::swift_bridge {

namespace {
// Inline SafeNarrow to avoid dependency on //flutter/lib/ui:ui
inline float SafeNarrow(double value) {
  if (std::isinf(value) || std::isnan(value)) {
    return static_cast<float>(value);
  }
  return std::clamp(static_cast<float>(value),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::max());
}

// Convert double matrix4 to DlMatrix
// Same as ToDlMatrix in flutter/lib/ui/painting/matrix.h
inline flutter::DlMatrix ToDlMatrix(const double* matrix4) {
  return flutter::DlMatrix::MakeColumn(
      SafeNarrow(matrix4[0]), SafeNarrow(matrix4[1]), SafeNarrow(matrix4[2]),
      SafeNarrow(matrix4[3]), SafeNarrow(matrix4[4]), SafeNarrow(matrix4[5]),
      SafeNarrow(matrix4[6]), SafeNarrow(matrix4[7]), SafeNarrow(matrix4[8]),
      SafeNarrow(matrix4[9]), SafeNarrow(matrix4[10]), SafeNarrow(matrix4[11]),
      SafeNarrow(matrix4[12]), SafeNarrow(matrix4[13]),
      SafeNarrow(matrix4[14]), SafeNarrow(matrix4[15]));
}

// Convert tile mode index to DlTileMode
inline flutter::DlTileMode ToDlTileMode(int tile_mode) {
  switch (tile_mode) {
    case 0:
      return flutter::DlTileMode::kClamp;
    case 1:
      return flutter::DlTileMode::kRepeat;
    case 2:
      return flutter::DlTileMode::kMirror;
    case 3:
      return flutter::DlTileMode::kDecal;
    default:
      return flutter::DlTileMode::kClamp;
  }
}

// Convert filter quality index to DlImageSampling
// Same as ImageFilter::SamplingFromIndex in flutter/lib/ui/painting/image_filter.cc
inline flutter::DlImageSampling SamplingFromIndex(int filter_quality_index) {
  switch (filter_quality_index) {
    case 0:
      return flutter::DlImageSampling::kNearestNeighbor;
    case 1:
      return flutter::DlImageSampling::kLinear;
    case 2:
      return flutter::DlImageSampling::kMipmapLinear;
    case 3:
      return flutter::DlImageSampling::kMipmapLinear;
    default:
      return flutter::DlImageSampling::kLinear;
  }
}
}  // namespace

// Pimpl implementation holding actual Flutter types
struct ImageShaderImpl {
  std::shared_ptr<flutter::DlColorSource> shader;
  sk_sp<const flutter::DlImage> image;

  ImageShaderImpl() : shader(nullptr), image(nullptr) {}
};

ImageShaderBridge::ImageShaderBridge()
    : impl_(new ImageShaderImpl()), debug_disposed_(false) {}

ImageShaderBridge::~ImageShaderBridge() {
  delete impl_;
}

bool ImageShaderBridge::InitWithImage(const ImageBridge* image_bridge,
                                      int tmx,
                                      int tmy,
                                      int filter_quality_index,
                                      const double* matrix4) {
  // The image_bridge provides access to the underlying DlImage through
  // the ImageBridgeImpl. Since ImageBridge uses pimpl and we can't access
  // the impl_ directly, we check if the image is valid by checking
  // dimensions. The actual DlImage needs to be obtained.
  //
  // Note: In the Dart implementation (image_shader.cc:30-31), the code
  // checks image->image()->isUIThreadSafe(). We replicate this by
  // checking if the image is not disposed and has valid dimensions.
  if (!image_bridge || image_bridge->IsDisposed()) {
    return false;
  }

  // Convert the matrix
  flutter::DlMatrix local_matrix = ToDlMatrix(matrix4);

  // Determine sampling
  bool sampling_is_locked = filter_quality_index >= 0;
  flutter::DlImageSampling sampling =
      sampling_is_locked ? SamplingFromIndex(filter_quality_index)
                         : flutter::DlImageSampling::kLinear;

  // We need to get the DlImage from ImageBridge.
  // Since ImageBridge uses pimpl, we access through a method.
  // The ImageBridge stores a DlImage internally. We need to access it
  // for creating the DlImageColorSource.
  //
  // IMPORTANT: We need to extend ImageBridge to expose the DlImage.
  // For now, we store the image bridge and create the shader using
  // DlColorSource::MakeImage.
  //
  // Since we can't directly access ImageBridgeImpl::image from here,
  // we need ImageBridge to provide a GetDlImage() method.
  // This is handled by adding GetDlImagePtr() to ImageBridge.
  const void* dl_image_ptr = image_bridge->GetDlImagePtr();
  if (!dl_image_ptr) {
    return false;
  }

  const sk_sp<const flutter::DlImage>* image_sp =
      static_cast<const sk_sp<const flutter::DlImage>*>(dl_image_ptr);
  impl_->image = *image_sp;

  if (!impl_->image || !impl_->image->isUIThreadSafe()) {
    impl_->image.reset();
    return false;
  }

  // Create the shader (same as image_shader.cc:47-49)
  impl_->shader = flutter::DlColorSource::MakeImage(
      impl_->image,
      ToDlTileMode(tmx),
      ToDlTileMode(tmy),
      sampling,
      &local_matrix);

  return true;
}

bool ImageShaderBridge::DebugDisposed() const {
  return debug_disposed_;
}

void ImageShaderBridge::Dispose() {
  // Match image_shader.cc:68-72
  debug_disposed_ = true;
  impl_->shader.reset();
  impl_->image.reset();
}

const void* ImageShaderBridge::GetShaderPtr() const {
  if (!impl_ || !impl_->shader) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->shader);
}

}  // namespace flutter::swift_bridge
