// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/canvas_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_blend_mode.h"
#include "flutter/display_list/dl_builder.h"
#include "flutter/display_list/dl_color.h"
#include "flutter/display_list/dl_op_flags.h"
#include "flutter/display_list/dl_paint.h"
#include "flutter/display_list/dl_sampling_options.h"
#include "flutter/display_list/effects/dl_color_filter.h"
#include "flutter/display_list/effects/dl_image_filter.h"
#include "flutter/display_list/effects/dl_image_filters.h"
#include "flutter/display_list/effects/dl_mask_filter.h"
#include "flutter/display_list/geometry/dl_geometry_types.h"
#include "flutter/display_list/geometry/dl_path.h"
#include "flutter/display_list/image/dl_image.h"
#include "flutter/impeller/geometry/rounding_radii.h"
#include "third_party/skia/include/core/SkPath.h"

#include "include/image_bridge.h"
#include "include/path_bridge.h"
#include "include/vertices_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

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

// Paint data layout constants - must match paint.cc and Dart painting.dart
constexpr int kIsAntiAliasIndex = 0;
constexpr int kColorRedIndex = 1;
constexpr int kColorGreenIndex = 2;
constexpr int kColorBlueIndex = 3;
constexpr int kColorAlphaIndex = 4;
constexpr int kColorSpaceIndex = 5;
constexpr int kBlendModeIndex = 6;
constexpr int kStyleIndex = 7;
constexpr int kStrokeWidthIndex = 8;
constexpr int kStrokeCapIndex = 9;
constexpr int kStrokeJoinIndex = 10;
constexpr int kStrokeMiterLimitIndex = 11;
// kFilterQualityIndex = 12 is unused (sampling handled at draw sites)
constexpr int kMaskFilterIndex = 13;
constexpr int kMaskFilterBlurStyleIndex = 14;
constexpr int kMaskFilterSigmaIndex = 15;
constexpr int kInvertColorIndex = 16;
// kDataByteCount = 68 (17 fields * 4 bytes) - documented for reference

constexpr uint32_t kBlendModeDefault =
    static_cast<uint32_t>(flutter::DlBlendMode::kSrcOver);
constexpr float kStrokeMiterLimitDefault = 4.0f;

enum MaskFilterType { kNull, kBlur };

// Filter quality to DlImageSampling mapping.
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
  }
  return kFilterQualities[filter_quality_index];
}

flutter::DlFilterMode FilterModeFromIndex(int filter_quality_index) {
  if (filter_quality_index <= 0) {
    return flutter::DlFilterMode::kNearest;
  }
  return flutter::DlFilterMode::kLinear;
}

flutter::DlColor ReadColor(const void* data) {
  const uint32_t* uint_data = static_cast<const uint32_t*>(data);
  const float* float_data = static_cast<const float*>(data);

  float red = float_data[kColorRedIndex];
  float green = float_data[kColorGreenIndex];
  float blue = float_data[kColorBlueIndex];
  // Invert alpha so 0 initialized buffer has default value
  float alpha = 1.f - float_data[kColorAlphaIndex];
  uint32_t colorspace = uint_data[kColorSpaceIndex];

  flutter::DlColor dl_color(alpha, red, green, blue,
                            static_cast<flutter::DlColorSpace>(colorspace));
  return dl_color.withColorSpace(flutter::DlColorSpace::kExtendedSRGB);
}

// Decode paint from raw data buffer.
// This replicates the logic from paint.cc Paint::paint().
const flutter::DlPaint* DecodePaint(
    flutter::DlPaint& paint,
    const void* paint_data,
    const flutter::DisplayListAttributeFlags& flags,
    flutter::DlTileMode tile_mode,
    const void* shader_ptr,
    const void* color_filter_ptr,
    const void* image_filter_ptr) {
  if (!paint_data) {
    return nullptr;
  }

  const uint32_t* uint_data = static_cast<const uint32_t*>(paint_data);
  const float* float_data = static_cast<const float*>(paint_data);

  // Extract shader
  if (flags.applies_shader()) {
    if (shader_ptr) {
      const auto& shader =
          *static_cast<const std::shared_ptr<flutter::DlColorSource>*>(
              shader_ptr);
      // Note: Sampling is already baked into the shader for image-based shaders.
      // For gradient shaders, sampling doesn't apply.
      paint.setColorSource(shader);
    } else {
      paint.setColorSource(nullptr);
    }
  }

  // Extract color filter
  if (flags.applies_color_filter()) {
    if (color_filter_ptr) {
      const auto& cf =
          *static_cast<const std::shared_ptr<const flutter::DlColorFilter>*>(
              color_filter_ptr);
      paint.setColorFilter(cf);
    } else {
      paint.setColorFilter(nullptr);
    }
  }

  // Extract image filter
  if (flags.applies_image_filter()) {
    if (image_filter_ptr) {
      const auto& imf =
          *static_cast<const std::shared_ptr<flutter::DlImageFilter>*>(
              image_filter_ptr);
      paint.setImageFilter(imf);
    } else {
      paint.setImageFilter(nullptr);
    }
  }

  if (flags.applies_anti_alias()) {
    paint.setAntiAlias(uint_data[kIsAntiAliasIndex] == 0);
  }

  if (flags.applies_alpha_or_color()) {
    paint.setColor(ReadColor(paint_data));
  }

  if (flags.applies_blend()) {
    uint32_t encoded = uint_data[kBlendModeIndex];
    uint32_t blend_mode = encoded ^ kBlendModeDefault;
    paint.setBlendMode(static_cast<flutter::DlBlendMode>(blend_mode));
  }

  if (flags.applies_style()) {
    uint32_t style = uint_data[kStyleIndex];
    paint.setDrawStyle(static_cast<flutter::DlDrawStyle>(style));
  }

  if (flags.is_stroked(paint.getDrawStyle())) {
    float stroke_width = float_data[kStrokeWidthIndex];
    paint.setStrokeWidth(stroke_width);

    float stroke_miter_limit = float_data[kStrokeMiterLimitIndex];
    paint.setStrokeMiter(stroke_miter_limit + kStrokeMiterLimitDefault);

    uint32_t stroke_cap = uint_data[kStrokeCapIndex];
    paint.setStrokeCap(static_cast<flutter::DlStrokeCap>(stroke_cap));

    uint32_t stroke_join = uint_data[kStrokeJoinIndex];
    paint.setStrokeJoin(static_cast<flutter::DlStrokeJoin>(stroke_join));
  }

  if (flags.applies_color_filter()) {
    paint.setInvertColors(uint_data[kInvertColorIndex] != 0);
  }

  if (flags.applies_mask_filter()) {
    switch (uint_data[kMaskFilterIndex]) {
      case kNull:
        paint.setMaskFilter(nullptr);
        break;
      case kBlur: {
        auto blur_style =
            static_cast<flutter::DlBlurStyle>(uint_data[kMaskFilterBlurStyleIndex]);
        double sigma = float_data[kMaskFilterSigmaIndex];
        paint.setMaskFilter(
            flutter::DlBlurMaskFilter::Make(blur_style, SafeNarrow(sigma)));
        break;
      }
    }
  }

  return &paint;
}

// Helper to decode an RRect from 12 floats (matching Dart RRect._getValue32)
// Layout: left, top, right, bottom, tlRadiusX, tlRadiusY, trRadiusX, trRadiusY,
//         brRadiusX, brRadiusY, blRadiusX, blRadiusY
flutter::DlRoundRect DecodeRRect(const float* data) {
  flutter::DlRect rect = flutter::DlRect::MakeLTRB(
      data[0], data[1], data[2], data[3]);
  // RRect radii order: tl, tr, br, bl (matching SkRRect and DlRoundRect)
  return flutter::DlRoundRect::MakeRectRadii(
      rect,
      {flutter::DlSize(data[4], data[5]),    // top-left
       flutter::DlSize(data[6], data[7]),    // top-right
       flutter::DlSize(data[8], data[9]),    // bottom-right
       flutter::DlSize(data[10], data[11])}  // bottom-left
  );
}

}  // namespace

// Pimpl implementation holding the actual Flutter DisplayListBuilder.
struct CanvasImpl {
  sk_sp<flutter::DisplayListBuilder> builder;

  CanvasImpl(flutter::DlRect bounds)
      : builder(sk_make_sp<flutter::DisplayListBuilder>(bounds, true)) {}
};

CanvasBridge::CanvasBridge(double left,
                           double top,
                           double right,
                           double bottom) {
  flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
      SafeNarrow(left), SafeNarrow(top),
      SafeNarrow(right), SafeNarrow(bottom));
  impl_ = new CanvasImpl(bounds);
}

CanvasBridge::~CanvasBridge() {
  delete impl_;
}

// MARK: - Save/Restore

void CanvasBridge::Save() {
  if (impl_->builder) {
    impl_->builder->Save();
  }
}

void CanvasBridge::SaveLayer(bool has_bounds,
                             double left,
                             double top,
                             double right,
                             double bottom,
                             const void* paint_data,
                             const void* shader_ptr,
                             const void* color_filter_ptr,
                             const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  const flutter::DlPaint* save_paint = DecodePaint(
      dl_paint, paint_data,
      flutter::DisplayListOpFlags::kSaveLayerWithPaintFlags,
      flutter::DlTileMode::kDecal,
      shader_ptr, color_filter_ptr, image_filter_ptr);

  if (has_bounds) {
    flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
        SafeNarrow(left), SafeNarrow(top),
        SafeNarrow(right), SafeNarrow(bottom));
    impl_->builder->SaveLayer(bounds, save_paint);
  } else {
    impl_->builder->SaveLayer(std::nullopt, save_paint);
  }
}

void CanvasBridge::Restore() {
  if (impl_->builder) {
    impl_->builder->Restore();
  }
}

void CanvasBridge::RestoreToCount(int count) {
  if (impl_->builder && count < GetSaveCount()) {
    impl_->builder->RestoreToCount(count);
  }
}

int CanvasBridge::GetSaveCount() {
  if (impl_->builder) {
    return impl_->builder->GetSaveCount();
  }
  return 0;
}

// MARK: - Transform

void CanvasBridge::Translate(double dx, double dy) {
  if (impl_->builder) {
    impl_->builder->Translate(SafeNarrow(dx), SafeNarrow(dy));
  }
}

void CanvasBridge::Scale(double sx, double sy) {
  if (impl_->builder) {
    impl_->builder->Scale(SafeNarrow(sx), SafeNarrow(sy));
  }
}

void CanvasBridge::Rotate(double radians) {
  if (impl_->builder) {
    impl_->builder->Rotate(SafeNarrow(radians) * 180.0f /
                           static_cast<float>(M_PI));
  }
}

void CanvasBridge::Skew(double sx, double sy) {
  if (impl_->builder) {
    impl_->builder->Skew(SafeNarrow(sx), SafeNarrow(sy));
  }
}

void CanvasBridge::Transform(const double* matrix4) {
  if (!impl_->builder || !matrix4) {
    return;
  }
  // Column-major to row-major conversion (same as canvas.cc)
  // clang-format off
  impl_->builder->TransformFullPerspective(
      SafeNarrow(matrix4[ 0]), SafeNarrow(matrix4[ 4]),
      SafeNarrow(matrix4[ 8]), SafeNarrow(matrix4[12]),
      SafeNarrow(matrix4[ 1]), SafeNarrow(matrix4[ 5]),
      SafeNarrow(matrix4[ 9]), SafeNarrow(matrix4[13]),
      SafeNarrow(matrix4[ 2]), SafeNarrow(matrix4[ 6]),
      SafeNarrow(matrix4[10]), SafeNarrow(matrix4[14]),
      SafeNarrow(matrix4[ 3]), SafeNarrow(matrix4[ 7]),
      SafeNarrow(matrix4[11]), SafeNarrow(matrix4[15]));
  // clang-format on
}

void CanvasBridge::GetTransform(double* out_matrix4) {
  if (!impl_->builder || !out_matrix4) {
    return;
  }
  flutter::DlMatrix matrix = impl_->builder->GetMatrix();
  for (int i = 0; i < 16; i++) {
    out_matrix4[i] = matrix.m[i];
  }
}

// MARK: - Clip

void CanvasBridge::ClipRect(double left,
                            double top,
                            double right,
                            double bottom,
                            int clip_op,
                            bool do_anti_alias) {
  if (impl_->builder) {
    impl_->builder->ClipRect(
        flutter::DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                  SafeNarrow(right), SafeNarrow(bottom)),
        static_cast<flutter::DlClipOp>(clip_op), do_anti_alias);
  }
}

void CanvasBridge::ClipRRect(const float* rrect_data, bool do_anti_alias) {
  if (!impl_->builder || !rrect_data) {
    return;
  }
  auto rrect = DecodeRRect(rrect_data);
  impl_->builder->ClipRoundRect(rrect, flutter::DlClipOp::kIntersect,
                                do_anti_alias);
}

void CanvasBridge::ClipRSuperellipse(double left,
                                     double top,
                                     double right,
                                     double bottom,
                                     double tl_radius_x,
                                     double tl_radius_y,
                                     double tr_radius_x,
                                     double tr_radius_y,
                                     double br_radius_x,
                                     double br_radius_y,
                                     double bl_radius_x,
                                     double bl_radius_y,
                                     bool do_anti_alias) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
      SafeNarrow(left), SafeNarrow(top),
      SafeNarrow(right), SafeNarrow(bottom)).GetPositive();
  impeller::RoundingRadii radii{
      .top_left = flutter::DlSize(SafeNarrow(tl_radius_x),
                                  SafeNarrow(tl_radius_y)),
      .top_right = flutter::DlSize(SafeNarrow(tr_radius_x),
                                   SafeNarrow(tr_radius_y)),
      .bottom_left = flutter::DlSize(SafeNarrow(bl_radius_x),
                                     SafeNarrow(bl_radius_y)),
      .bottom_right = flutter::DlSize(SafeNarrow(br_radius_x),
                                      SafeNarrow(br_radius_y)),
  };
  auto rse = flutter::DlRoundSuperellipse::MakeRectRadii(bounds, radii);
  impl_->builder->ClipRoundSuperellipse(rse, flutter::DlClipOp::kIntersect,
                                        do_anti_alias);
}

void CanvasBridge::ClipPath(const PathBridge* path, bool do_anti_alias) {
  if (!impl_->builder || !path) {
    return;
  }
  const void* path_ptr = path->GetSkPathPtr();
  if (!path_ptr) {
    return;
  }
  const auto& sk_path = *static_cast<const SkPath*>(path_ptr);
  impl_->builder->ClipPath(flutter::DlPath(sk_path), flutter::DlClipOp::kIntersect,
                           do_anti_alias);
}

void CanvasBridge::GetLocalClipBounds(double* out_bounds) {
  if (!impl_->builder || !out_bounds) {
    return;
  }
  flutter::DlRect bounds = impl_->builder->GetLocalClipCoverage();
  out_bounds[0] = bounds.GetLeft();
  out_bounds[1] = bounds.GetTop();
  out_bounds[2] = bounds.GetRight();
  out_bounds[3] = bounds.GetBottom();
}

void CanvasBridge::GetDestinationClipBounds(double* out_bounds) {
  if (!impl_->builder || !out_bounds) {
    return;
  }
  flutter::DlRect bounds = impl_->builder->GetDestinationClipCoverage();
  out_bounds[0] = bounds.GetLeft();
  out_bounds[1] = bounds.GetTop();
  out_bounds[2] = bounds.GetRight();
  out_bounds[3] = bounds.GetBottom();
}

// MARK: - Drawing Methods

void CanvasBridge::DrawColor(int color, int blend_mode) {
  if (impl_->builder) {
    impl_->builder->DrawColor(
        flutter::DlColor(static_cast<uint32_t>(color)),
        static_cast<flutter::DlBlendMode>(blend_mode));
  }
}

void CanvasBridge::DrawLine(double x1,
                            double y1,
                            double x2,
                            double y2,
                            const void* paint_data,
                            const void* shader_ptr,
                            const void* color_filter_ptr,
                            const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawLineFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawLine(
      flutter::DlPoint(SafeNarrow(x1), SafeNarrow(y1)),
      flutter::DlPoint(SafeNarrow(x2), SafeNarrow(y2)), dl_paint);
}

void CanvasBridge::DrawPaint(const void* paint_data,
                             const void* shader_ptr,
                             const void* color_filter_ptr,
                             const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawPaintFlags,
              flutter::DlTileMode::kClamp,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawPaint(dl_paint);
}

void CanvasBridge::DrawRect(double left,
                            double top,
                            double right,
                            double bottom,
                            const void* paint_data,
                            const void* shader_ptr,
                            const void* color_filter_ptr,
                            const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawRectFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawRect(
      flutter::DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                SafeNarrow(right), SafeNarrow(bottom)),
      dl_paint);
}

void CanvasBridge::DrawRRect(const float* rrect_data,
                             const void* paint_data,
                             const void* shader_ptr,
                             const void* color_filter_ptr,
                             const void* image_filter_ptr) {
  if (!impl_->builder || !rrect_data) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawRRectFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawRoundRect(DecodeRRect(rrect_data), dl_paint);
}

void CanvasBridge::DrawDRRect(const float* outer_data,
                              const float* inner_data,
                              const void* paint_data,
                              const void* shader_ptr,
                              const void* color_filter_ptr,
                              const void* image_filter_ptr) {
  if (!impl_->builder || !outer_data || !inner_data) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawDRRectFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawDiffRoundRect(DecodeRRect(outer_data),
                                    DecodeRRect(inner_data), dl_paint);
}

void CanvasBridge::DrawRSuperellipse(double left,
                                     double top,
                                     double right,
                                     double bottom,
                                     double tl_radius_x,
                                     double tl_radius_y,
                                     double tr_radius_x,
                                     double tr_radius_y,
                                     double br_radius_x,
                                     double br_radius_y,
                                     double bl_radius_x,
                                     double bl_radius_y,
                                     const void* paint_data,
                                     const void* shader_ptr,
                                     const void* color_filter_ptr,
                                     const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawRSuperellipseFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);

  flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
      SafeNarrow(left), SafeNarrow(top),
      SafeNarrow(right), SafeNarrow(bottom)).GetPositive();
  impeller::RoundingRadii radii{
      .top_left = flutter::DlSize(SafeNarrow(tl_radius_x),
                                  SafeNarrow(tl_radius_y)),
      .top_right = flutter::DlSize(SafeNarrow(tr_radius_x),
                                   SafeNarrow(tr_radius_y)),
      .bottom_left = flutter::DlSize(SafeNarrow(bl_radius_x),
                                     SafeNarrow(bl_radius_y)),
      .bottom_right = flutter::DlSize(SafeNarrow(br_radius_x),
                                      SafeNarrow(br_radius_y)),
  };
  auto rse = flutter::DlRoundSuperellipse::MakeRectRadii(bounds, radii);
  impl_->builder->DrawRoundSuperellipse(rse, dl_paint);
}

void CanvasBridge::DrawOval(double left,
                            double top,
                            double right,
                            double bottom,
                            const void* paint_data,
                            const void* shader_ptr,
                            const void* color_filter_ptr,
                            const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawOvalFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawOval(
      flutter::DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                SafeNarrow(right), SafeNarrow(bottom)),
      dl_paint);
}

void CanvasBridge::DrawCircle(double x,
                              double y,
                              double radius,
                              const void* paint_data,
                              const void* shader_ptr,
                              const void* color_filter_ptr,
                              const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawCircleFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawCircle(
      flutter::DlPoint(SafeNarrow(x), SafeNarrow(y)),
      SafeNarrow(radius), dl_paint);
}

void CanvasBridge::DrawArc(double left,
                           double top,
                           double right,
                           double bottom,
                           double start_angle,
                           double sweep_angle,
                           bool use_center,
                           const void* paint_data,
                           const void* shader_ptr,
                           const void* color_filter_ptr,
                           const void* image_filter_ptr) {
  if (!impl_->builder) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              use_center
                  ? flutter::DisplayListOpFlags::kDrawArcWithCenterFlags
                  : flutter::DisplayListOpFlags::kDrawArcNoCenterFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  impl_->builder->DrawArc(
      flutter::DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                SafeNarrow(right), SafeNarrow(bottom)),
      SafeNarrow(start_angle) * 180.0f / static_cast<float>(M_PI),
      SafeNarrow(sweep_angle) * 180.0f / static_cast<float>(M_PI),
      use_center, dl_paint);
}

void CanvasBridge::DrawPath(const PathBridge* path,
                            const void* paint_data,
                            const void* shader_ptr,
                            const void* color_filter_ptr,
                            const void* image_filter_ptr) {
  if (!impl_->builder || !path) {
    return;
  }
  const void* path_ptr = path->GetSkPathPtr();
  if (!path_ptr) {
    return;
  }
  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawPathFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);
  const auto& sk_path = *static_cast<const SkPath*>(path_ptr);
  impl_->builder->DrawPath(flutter::DlPath(sk_path), dl_paint);
}

const char* CanvasBridge::DrawImage(const ImageBridge* image,
                                    double x,
                                    double y,
                                    const void* paint_data,
                                    const void* shader_ptr,
                                    const void* color_filter_ptr,
                                    const void* image_filter_ptr,
                                    int filter_quality_index) {
  if (!image) {
    error_message_ = "Canvas.drawImage called with non-genuine Image.";
    return error_message_.c_str();
  }

  const void* dl_image_ptr = image->GetDlImagePtr();
  if (!dl_image_ptr) {
    return "";  // No error, just nothing to draw
  }
  const auto& dl_image =
      *static_cast<const sk_sp<flutter::DlImage>*>(dl_image_ptr);
  if (!dl_image) {
    return "";
  }
  auto error = dl_image->get_error();
  if (error) {
    error_message_ = error.value();
    return error_message_.c_str();
  }

  auto sampling = SamplingFromIndex(filter_quality_index);
  if (impl_->builder) {
    flutter::DlPaint dl_paint;
    const flutter::DlPaint* opt_paint = DecodePaint(
        dl_paint, paint_data,
        flutter::DisplayListOpFlags::kDrawImageWithPaintFlags,
        flutter::DlTileMode::kClamp,
        shader_ptr, color_filter_ptr, image_filter_ptr);
    impl_->builder->DrawImage(
        dl_image, flutter::DlPoint(SafeNarrow(x), SafeNarrow(y)),
        sampling, opt_paint);
  }
  return "";
}

const char* CanvasBridge::DrawImageRect(const ImageBridge* image,
                                        double src_left,
                                        double src_top,
                                        double src_right,
                                        double src_bottom,
                                        double dst_left,
                                        double dst_top,
                                        double dst_right,
                                        double dst_bottom,
                                        const void* paint_data,
                                        const void* shader_ptr,
                                        const void* color_filter_ptr,
                                        const void* image_filter_ptr,
                                        int filter_quality_index) {
  if (!image) {
    error_message_ = "Canvas.drawImageRect called with non-genuine Image.";
    return error_message_.c_str();
  }

  const void* dl_image_ptr = image->GetDlImagePtr();
  if (!dl_image_ptr) {
    return "";
  }
  const auto& dl_image =
      *static_cast<const sk_sp<flutter::DlImage>*>(dl_image_ptr);
  if (!dl_image) {
    return "";
  }
  auto error = dl_image->get_error();
  if (error) {
    error_message_ = error.value();
    return error_message_.c_str();
  }

  flutter::DlRect src = flutter::DlRect::MakeLTRB(
      SafeNarrow(src_left), SafeNarrow(src_top),
      SafeNarrow(src_right), SafeNarrow(src_bottom));
  flutter::DlRect dst = flutter::DlRect::MakeLTRB(
      SafeNarrow(dst_left), SafeNarrow(dst_top),
      SafeNarrow(dst_right), SafeNarrow(dst_bottom));
  auto sampling = SamplingFromIndex(filter_quality_index);
  if (impl_->builder) {
    flutter::DlPaint dl_paint;
    const flutter::DlPaint* opt_paint = DecodePaint(
        dl_paint, paint_data,
        flutter::DisplayListOpFlags::kDrawImageRectWithPaintFlags,
        flutter::DlTileMode::kClamp,
        shader_ptr, color_filter_ptr, image_filter_ptr);
    impl_->builder->DrawImageRect(dl_image, src, dst, sampling, opt_paint,
                                  flutter::DlSrcRectConstraint::kFast);
  }
  return "";
}

const char* CanvasBridge::DrawImageNine(const ImageBridge* image,
                                        double center_left,
                                        double center_top,
                                        double center_right,
                                        double center_bottom,
                                        double dst_left,
                                        double dst_top,
                                        double dst_right,
                                        double dst_bottom,
                                        const void* paint_data,
                                        const void* shader_ptr,
                                        const void* color_filter_ptr,
                                        const void* image_filter_ptr,
                                        int filter_quality_index) {
  if (!image) {
    error_message_ = "Canvas.drawImageNine called with non-genuine Image.";
    return error_message_.c_str();
  }

  const void* dl_image_ptr = image->GetDlImagePtr();
  if (!dl_image_ptr) {
    return "";
  }
  const auto& dl_image =
      *static_cast<const sk_sp<flutter::DlImage>*>(dl_image_ptr);
  if (!dl_image) {
    return "";
  }
  auto error = dl_image->get_error();
  if (error) {
    error_message_ = error.value();
    return error_message_.c_str();
  }

  flutter::DlRect center = flutter::DlRect::MakeLTRB(
      SafeNarrow(center_left), SafeNarrow(center_top),
      SafeNarrow(center_right), SafeNarrow(center_bottom));
  flutter::DlIRect icenter = flutter::DlIRect::Round(center);
  flutter::DlRect dst = flutter::DlRect::MakeLTRB(
      SafeNarrow(dst_left), SafeNarrow(dst_top),
      SafeNarrow(dst_right), SafeNarrow(dst_bottom));
  auto filter = FilterModeFromIndex(filter_quality_index);
  if (impl_->builder) {
    flutter::DlPaint dl_paint;
    const flutter::DlPaint* opt_paint = DecodePaint(
        dl_paint, paint_data,
        flutter::DisplayListOpFlags::kDrawImageNineWithPaintFlags,
        flutter::DlTileMode::kClamp,
        shader_ptr, color_filter_ptr, image_filter_ptr);
    impl_->builder->DrawImageNine(dl_image, icenter, dst, filter, opt_paint);
  }
  return "";
}

void CanvasBridge::DrawDisplayList(const void* display_list_ptr) {
  if (!impl_->builder || !display_list_ptr) {
    return;
  }
  const auto& display_list =
      *static_cast<const sk_sp<flutter::DisplayList>*>(display_list_ptr);
  if (display_list) {
    impl_->builder->DrawDisplayList(display_list);
  }
}

void CanvasBridge::DrawPoints(int point_mode,
                              const float* points,
                              int point_count,
                              const void* paint_data,
                              const void* shader_ptr,
                              const void* color_filter_ptr,
                              const void* image_filter_ptr) {
  if (!impl_->builder || !points || point_count <= 0) {
    return;
  }

  auto dl_point_mode = static_cast<flutter::DlPointMode>(point_mode);
  flutter::DlPaint dl_paint;

  const auto& flags =
      dl_point_mode == flutter::DlPointMode::kPoints
          ? flutter::DisplayListOpFlags::kDrawPointsAsPointsFlags
          : dl_point_mode == flutter::DlPointMode::kLines
                ? flutter::DisplayListOpFlags::kDrawPointsAsLinesFlags
                : flutter::DisplayListOpFlags::kDrawPointsAsPolygonFlags;

  DecodePaint(dl_paint, paint_data, flags, flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);

  impl_->builder->DrawPoints(
      dl_point_mode,
      point_count / 2,  // DlPoints have 2 floats each
      reinterpret_cast<const flutter::DlPoint*>(points), dl_paint);
}

void CanvasBridge::DrawVertices(const VerticesBridge* vertices,
                                int blend_mode,
                                const void* paint_data,
                                const void* shader_ptr,
                                const void* color_filter_ptr,
                                const void* image_filter_ptr) {
  if (!impl_->builder || !vertices) {
    return;
  }

  const void* vertices_ptr = vertices->GetDlVerticesPtr();
  if (!vertices_ptr) {
    return;
  }

  flutter::DlPaint dl_paint;
  DecodePaint(dl_paint, paint_data,
              flutter::DisplayListOpFlags::kDrawVerticesFlags,
              flutter::DlTileMode::kDecal,
              shader_ptr, color_filter_ptr, image_filter_ptr);

  const auto& dl_vertices =
      *static_cast<const std::shared_ptr<flutter::DlVertices>*>(vertices_ptr);
  impl_->builder->DrawVertices(dl_vertices,
                               static_cast<flutter::DlBlendMode>(blend_mode),
                               dl_paint);
}

const char* CanvasBridge::DrawAtlas(const ImageBridge* atlas,
                                    const float* rst_transforms,
                                    const float* rects,
                                    int rect_count,
                                    const int* colors,
                                    int color_count,
                                    int blend_mode,
                                    const float* cull_rect,
                                    const void* paint_data,
                                    const void* shader_ptr,
                                    const void* color_filter_ptr,
                                    const void* image_filter_ptr,
                                    int filter_quality_index) {
  if (!atlas) {
    error_message_ =
        "Canvas.drawAtlas or Canvas.drawRawAtlas called with "
        "non-genuine Image.";
    return error_message_.c_str();
  }

  const void* dl_image_ptr = atlas->GetDlImagePtr();
  if (!dl_image_ptr) {
    return "";
  }
  const auto& dl_image =
      *static_cast<const sk_sp<flutter::DlImage>*>(dl_image_ptr);
  if (!dl_image) {
    return "";
  }
  auto error = dl_image->get_error();
  if (error) {
    error_message_ = error.value();
    return error_message_.c_str();
  }

  auto sampling = SamplingFromIndex(filter_quality_index);

  if (impl_->builder) {
    // Convert colors from int32 to DlColor
    std::vector<flutter::DlColor> dl_colors;
    if (colors && color_count > 0) {
      dl_colors.resize(color_count);
      for (int i = 0; i < color_count; i++) {
        dl_colors[i] = flutter::DlColor(static_cast<uint32_t>(colors[i]));
      }
    }

    flutter::DlPaint dl_paint;
    const flutter::DlPaint* opt_paint = DecodePaint(
        dl_paint, paint_data,
        flutter::DisplayListOpFlags::kDrawAtlasWithPaintFlags,
        flutter::DlTileMode::kClamp,
        shader_ptr, color_filter_ptr, image_filter_ptr);

    impl_->builder->DrawAtlas(
        dl_image,
        reinterpret_cast<const flutter::DlRSTransform*>(rst_transforms),
        reinterpret_cast<const flutter::DlRect*>(rects),
        dl_colors.empty() ? nullptr : dl_colors.data(),
        rect_count / 4,  // DlRect has 4 floats
        static_cast<flutter::DlBlendMode>(blend_mode), sampling,
        reinterpret_cast<const flutter::DlRect*>(cull_rect), opt_paint);
  }
  return "";
}

void CanvasBridge::DrawShadow(const PathBridge* path,
                              int color,
                              double elevation,
                              bool transparent_occluder) {
  if (!impl_->builder || !path) {
    return;
  }
  const void* path_ptr = path->GetSkPathPtr();
  if (!path_ptr) {
    return;
  }
  const auto& sk_path = *static_cast<const SkPath*>(path_ptr);
  // Use default DPR of 1.0 - the actual DPR will come from the view metrics
  // in the full implementation. For now, use 1.0 as the Swift layer doesn't
  // have access to UIDartState.
  float dpr = 1.0f;
  impl_->builder->DrawShadow(flutter::DlPath(sk_path),
                              flutter::DlColor(static_cast<uint32_t>(color)),
                              SafeNarrow(elevation), transparent_occluder, dpr);
}

void CanvasBridge::Invalidate() {
  impl_->builder = nullptr;
}

const void* CanvasBridge::GetDisplayListBuilderPtr() const {
  if (!impl_ || !impl_->builder) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->builder);
}

}  // namespace flutter::swift_bridge
