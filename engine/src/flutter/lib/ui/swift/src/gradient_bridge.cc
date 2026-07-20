// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/gradient_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/effects/dl_color_source.h"
#include "flutter/display_list/dl_tile_mode.h"
#include "flutter/impeller/geometry/matrix.h"

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
  // Dart/Swift matrix4 is column-major (same as DlMatrix)
  return flutter::DlMatrix::MakeColumn(
      SafeNarrow(matrix4[0]), SafeNarrow(matrix4[1]), SafeNarrow(matrix4[2]),
      SafeNarrow(matrix4[3]), SafeNarrow(matrix4[4]), SafeNarrow(matrix4[5]),
      SafeNarrow(matrix4[6]), SafeNarrow(matrix4[7]), SafeNarrow(matrix4[8]),
      SafeNarrow(matrix4[9]), SafeNarrow(matrix4[10]), SafeNarrow(matrix4[11]),
      SafeNarrow(matrix4[12]), SafeNarrow(matrix4[13]), SafeNarrow(matrix4[14]),
      SafeNarrow(matrix4[15]));
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
}  // namespace

// Pimpl implementation holding actual Flutter types
struct GradientImpl {
  std::shared_ptr<flutter::DlColorSource> shader;

  GradientImpl() : shader(nullptr) {}
};

GradientBridge::GradientBridge() : impl_(new GradientImpl()), debug_disposed_(false) {}

GradientBridge::~GradientBridge() {
  delete impl_;
}

void GradientBridge::InitLinear(double from_x,
                                double from_y,
                                double to_x,
                                double to_y,
                                const float* colors,
                                int color_count,
                                const float* color_stops,
                                int stop_count,
                                int tile_mode,
                                const double* matrix4) {
  flutter::DlMatrix dl_matrix;
  bool has_matrix = matrix4 != nullptr;
  if (has_matrix) {
    dl_matrix = ToDlMatrix(matrix4);
  }

  flutter::DlPoint p0 = flutter::DlPoint(SafeNarrow(from_x), SafeNarrow(from_y));
  flutter::DlPoint p1 = flutter::DlPoint(SafeNarrow(to_x), SafeNarrow(to_y));

  impl_->shader = flutter::DlColorSource::MakeLinear(
      p0, p1, color_count, colors, color_stops, ToDlTileMode(tile_mode),
      has_matrix ? &dl_matrix : nullptr);
}

void GradientBridge::InitRadial(double center_x,
                                double center_y,
                                double radius,
                                const float* colors,
                                int color_count,
                                const float* color_stops,
                                int stop_count,
                                int tile_mode,
                                const double* matrix4) {
  flutter::DlMatrix dl_matrix;
  bool has_matrix = matrix4 != nullptr;
  if (has_matrix) {
    dl_matrix = ToDlMatrix(matrix4);
  }

  impl_->shader = flutter::DlColorSource::MakeRadial(
      flutter::DlPoint(SafeNarrow(center_x), SafeNarrow(center_y)),
      SafeNarrow(radius), color_count, colors, color_stops,
      ToDlTileMode(tile_mode), has_matrix ? &dl_matrix : nullptr);
}

void GradientBridge::InitConical(double focal_x,
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
                                 const double* matrix4) {
  flutter::DlMatrix dl_matrix;
  bool has_matrix = matrix4 != nullptr;
  if (has_matrix) {
    dl_matrix = ToDlMatrix(matrix4);
  }

  impl_->shader = flutter::DlColorSource::MakeConical(
      flutter::DlPoint(SafeNarrow(focal_x), SafeNarrow(focal_y)),
      SafeNarrow(focal_radius),
      flutter::DlPoint(SafeNarrow(center_x), SafeNarrow(center_y)),
      SafeNarrow(radius), color_count, colors, color_stops,
      ToDlTileMode(tile_mode), has_matrix ? &dl_matrix : nullptr);
}

void GradientBridge::InitSweep(double center_x,
                               double center_y,
                               const float* colors,
                               int color_count,
                               const float* color_stops,
                               int stop_count,
                               int tile_mode,
                               double start_angle,
                               double end_angle,
                               const double* matrix4) {
  flutter::DlMatrix dl_matrix;
  bool has_matrix = matrix4 != nullptr;
  if (has_matrix) {
    dl_matrix = ToDlMatrix(matrix4);
  }

  // Convert radians to degrees (same as gradient.cc)
  float start_degrees =
      SafeNarrow(start_angle) * 180.0f / static_cast<float>(M_PI);
  float end_degrees =
      SafeNarrow(end_angle) * 180.0f / static_cast<float>(M_PI);

  impl_->shader = flutter::DlColorSource::MakeSweep(
      flutter::DlPoint(SafeNarrow(center_x), SafeNarrow(center_y)),
      start_degrees, end_degrees, color_count, colors, color_stops,
      ToDlTileMode(tile_mode), has_matrix ? &dl_matrix : nullptr);
}

bool GradientBridge::DebugDisposed() const {
  return debug_disposed_;
}

void GradientBridge::Dispose() {
  debug_disposed_ = true;
  impl_->shader.reset();
}

const void* GradientBridge::GetShaderPtr() const {
  if (!impl_ || !impl_->shader) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->shader);
}

}  // namespace flutter::swift_bridge
