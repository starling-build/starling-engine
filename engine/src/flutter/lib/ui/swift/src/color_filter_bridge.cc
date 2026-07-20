// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/color_filter_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/effects/dl_color_filter.h"

#include <cstring>
#include <memory>

namespace flutter::swift_bridge {

/// Pimpl implementation holding the actual Flutter DlColorFilter.
///
/// **Dart Source:** `painting.dart:4135-4181` (_ColorFilter native class)
///
/// The Dart _ColorFilter class wraps a native DlColorFilter via
/// NativeFieldWrapperClass1. In Swift, we use pimpl to hold the
/// shared_ptr<DlColorFilter> directly.
struct ColorFilterImpl {
  std::shared_ptr<const flutter::DlColorFilter> filter;

  ColorFilterImpl() : filter(nullptr) {}
};

ColorFilterBridge::ColorFilterBridge() : impl_(new ColorFilterImpl()) {}

ColorFilterBridge::~ColorFilterBridge() {
  delete impl_;
}

void ColorFilterBridge::InitMode(int color, int blend_mode) {
  // Matches ColorFilter::initMode in color_filter.cc
  // DlColor is constructed from the 32-bit ARGB integer
  // DlBlendMode is cast from the integer index
  impl_->filter = flutter::DlColorFilter::MakeBlend(
      static_cast<flutter::DlColor>(color),
      static_cast<flutter::DlBlendMode>(blend_mode));
}

void ColorFilterBridge::InitMatrix(const float* matrix) {
  // Matches ColorFilter::initMatrix in color_filter.cc
  // Flutter defines the matrix with the last column (translate) biased by 255.
  // Skia expects normalized values (0...1), so we post-scale before calling
  // the factory.
  float normalized[20];
  memcpy(normalized, matrix, sizeof(normalized));
  normalized[4] *= 1.0f / 255;
  normalized[9] *= 1.0f / 255;
  normalized[14] *= 1.0f / 255;
  normalized[19] *= 1.0f / 255;
  impl_->filter = flutter::DlColorFilter::MakeMatrix(normalized);
}

void ColorFilterBridge::InitLinearToSrgbGamma() {
  // Matches ColorFilter::initLinearToSrgbGamma in color_filter.cc
  impl_->filter = flutter::DlColorFilter::MakeLinearToSrgbGamma();
}

void ColorFilterBridge::InitSrgbToLinearGamma() {
  // Matches ColorFilter::initSrgbToLinearGamma in color_filter.cc
  impl_->filter = flutter::DlColorFilter::MakeSrgbToLinearGamma();
}

const void* ColorFilterBridge::GetFilterPtr() const {
  if (!impl_ || !impl_->filter) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->filter);
}

}  // namespace flutter::swift_bridge
