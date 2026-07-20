// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/image_filter_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_sampling_options.h"
#include "flutter/display_list/effects/dl_color_filter.h"
#include "flutter/display_list/effects/dl_image_filters.h"

#include <algorithm>
#include <array>
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

// Filter quality to DlImageSampling mapping.
// Matches ImageFilter::SamplingFromIndex in image_filter.cc:29-45
static const std::array<flutter::DlImageSampling, 4> kFilterQualities = {
    flutter::DlImageSampling::kNearestNeighbor,
    flutter::DlImageSampling::kLinear,
    flutter::DlImageSampling::kMipmapLinear,
    flutter::DlImageSampling::kCubic,
};

flutter::DlImageSampling SamplingFromIndex(int filter_quality_index) {
  if (filter_quality_index < 0) {
    return kFilterQualities.front();
  } else if (static_cast<size_t>(filter_quality_index) >=
             kFilterQualities.size()) {
    return kFilterQualities.back();
  } else {
    return kFilterQualities[filter_quality_index];
  }
}
}  // namespace

/// Pimpl implementation holding the actual Flutter DlImageFilter.
///
/// **Dart Source:** `painting.dart:4511-4607` (_ImageFilter native class)
///
/// The Dart _ImageFilter class wraps a native DlImageFilter via
/// NativeFieldWrapperClass1. In Swift, we use pimpl to hold the
/// shared_ptr<DlImageFilter> directly.
struct ImageFilterImpl {
  std::shared_ptr<flutter::DlImageFilter> filter;
  mutable std::shared_ptr<flutter::DlImageFilter> resolved_filter;
  bool is_dynamic_tile_mode = false;

  ImageFilterImpl() : filter(nullptr) {}
};

ImageFilterBridge::ImageFilterBridge() : impl_(new ImageFilterImpl()) {}

ImageFilterBridge::~ImageFilterBridge() {
  delete impl_;
}

void ImageFilterBridge::InitBlur(double sigma_x,
                                 double sigma_y,
                                 int tile_mode_index) {
  // Matches ImageFilter::initBlur in image_filter.cc:72-89
  flutter::DlTileMode tile_mode;
  bool is_dynamic;
  if (tile_mode_index < 0) {
    is_dynamic = true;
    tile_mode = flutter::DlTileMode::kClamp;
  } else {
    is_dynamic = false;
    tile_mode = static_cast<flutter::DlTileMode>(tile_mode_index);
  }
  impl_->filter = flutter::DlBlurImageFilter::Make(
      SafeNarrow(sigma_x), SafeNarrow(sigma_y), tile_mode);
  // If it was a NOP filter, don't bother processing dynamic substitutions
  impl_->is_dynamic_tile_mode = is_dynamic && impl_->filter;
}

void ImageFilterBridge::InitDilate(double radius_x, double radius_y) {
  // Matches ImageFilter::initDilate in image_filter.cc:91-95
  impl_->is_dynamic_tile_mode = false;
  impl_->filter = flutter::DlDilateImageFilter::Make(
      SafeNarrow(radius_x), SafeNarrow(radius_y));
}

void ImageFilterBridge::InitErode(double radius_x, double radius_y) {
  // Matches ImageFilter::initErode in image_filter.cc:97-101
  impl_->is_dynamic_tile_mode = false;
  impl_->filter = flutter::DlErodeImageFilter::Make(
      SafeNarrow(radius_x), SafeNarrow(radius_y));
}

void ImageFilterBridge::InitMatrix(const double* matrix4,
                                   int filter_quality_index) {
  // Matches ImageFilter::initMatrix in image_filter.cc:103-108
  // Constructs DlMatrix from 16-element column-major double array
  // using SafeNarrow for double->float conversion.
  impl_->is_dynamic_tile_mode = false;
  auto sampling = SamplingFromIndex(filter_quality_index);
  // clang-format off
  flutter::DlMatrix matrix = flutter::DlMatrix::MakeColumn(
      SafeNarrow(matrix4[ 0]), SafeNarrow(matrix4[ 1]),
      SafeNarrow(matrix4[ 2]), SafeNarrow(matrix4[ 3]),
      SafeNarrow(matrix4[ 4]), SafeNarrow(matrix4[ 5]),
      SafeNarrow(matrix4[ 6]), SafeNarrow(matrix4[ 7]),
      SafeNarrow(matrix4[ 8]), SafeNarrow(matrix4[ 9]),
      SafeNarrow(matrix4[10]), SafeNarrow(matrix4[11]),
      SafeNarrow(matrix4[12]), SafeNarrow(matrix4[13]),
      SafeNarrow(matrix4[14]), SafeNarrow(matrix4[15])
  );
  // clang-format on
  impl_->filter = flutter::DlMatrixImageFilter::Make(matrix, sampling);
}

void ImageFilterBridge::InitColorFilter(ColorFilterBridge* color_filter) {
  // Matches ImageFilter::initColorFilter in image_filter.cc:110-114
  impl_->is_dynamic_tile_mode = false;
  // color_filter may be null (matching Dart's _ColorFilter? parameter)
  if (color_filter == nullptr) {
    impl_->filter = nullptr;
  } else {
    const void* filter_ptr = color_filter->GetFilterPtr();
    if (filter_ptr) {
      const auto& dl_color_filter =
          *static_cast<const std::shared_ptr<const flutter::DlColorFilter>*>(
              filter_ptr);
      impl_->filter =
          flutter::DlColorFilterImageFilter::Make(dl_color_filter);
    } else {
      impl_->filter = nullptr;
    }
  }
}

void ImageFilterBridge::InitComposed(ImageFilterBridge* outer,
                                     ImageFilterBridge* inner) {
  // Matches ImageFilter::initComposeFilter in image_filter.cc:116-121
  impl_->is_dynamic_tile_mode = false;
  if (outer && inner) {
    impl_->filter = flutter::DlComposeImageFilter::Make(
        outer->impl_->filter, inner->impl_->filter);
  } else {
    impl_->filter = nullptr;
  }
}

void ImageFilterBridge::InitShader(FragmentShaderBridge* shader) {
  // Matches ImageFilter::initShader in image_filter.cc:123-126
  // Uses FragmentShaderBridge::MakeImageFilter() which returns an opaque
  // void* to a shared_ptr<DlImageFilter>.
  impl_->is_dynamic_tile_mode = false;
  if (shader) {
    void* filter_ptr = shader->MakeImageFilter();
    if (filter_ptr) {
      // MakeImageFilter returns a new'd shared_ptr<DlImageFilter>
      auto* sp = static_cast<std::shared_ptr<flutter::DlImageFilter>*>(
          filter_ptr);
      impl_->filter = *sp;
      delete sp;
    } else {
      impl_->filter = nullptr;
    }
  } else {
    impl_->filter = nullptr;
  }
}

bool ImageFilterBridge::Equals(const ImageFilterBridge* other) const {
  // Matches ImageFilter::equals in image_filter.cc:128-130
  if (!other) {
    return false;
  }
  return impl_->filter == other->impl_->filter;
}

bool ImageFilterBridge::IsDynamicTileMode() const {
  return impl_->is_dynamic_tile_mode;
}

const void* ImageFilterBridge::GetFilterPtr(int tile_mode_index) const {
  if (!impl_ || !impl_->filter) {
    return nullptr;
  }
  if (impl_->is_dynamic_tile_mode && tile_mode_index >= 0) {
    // For dynamic tile mode filters, resolve by creating a new filter with
    // the specified tile mode. Matches ImageFilter::filter() in image_filter.cc.
    auto dl_tile_mode = static_cast<flutter::DlTileMode>(tile_mode_index);
    const auto* blur = impl_->filter->asBlur();
    if (blur && blur->tile_mode() != dl_tile_mode) {
      // Create a resolved filter with the specified tile mode.
      // Store in resolved_filter_ so the pointer stays valid.
      impl_->resolved_filter = flutter::DlBlurImageFilter::Make(
          blur->sigma_x(), blur->sigma_y(), dl_tile_mode);
      return static_cast<const void*>(&impl_->resolved_filter);
    }
  }
  return static_cast<const void*>(&impl_->filter);
}

}  // namespace flutter::swift_bridge
