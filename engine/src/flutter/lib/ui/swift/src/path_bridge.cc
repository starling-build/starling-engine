// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/path_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/geometry/dl_geometry_conversions.h"
#include "flutter/display_list/geometry/dl_geometry_types.h"
#include "flutter/display_list/geometry/dl_path_builder.h"
#include "flutter/impeller/geometry/round_superellipse_param.h"
#include "flutter/impeller/geometry/rounding_radii.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "third_party/skia/include/pathops/SkPathOps.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

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

// Mappings from SkMatrix-index to input-index.
// Same as kSkMatrixIndexToMatrix4Index in flutter/lib/ui/painting/matrix.cc
static const int kSkMatrixIndexToMatrix4Index[] = {
    // clang-format off
    0, 4, 12,
    1, 5, 13,
    3, 7, 15,
    // clang-format on
};

// Convert double matrix4 (16 values) to SkMatrix (3x3).
// Same logic as ToSkMatrix in flutter/lib/ui/painting/matrix.cc
inline SkMatrix ToSkMatrix(const double* matrix4) {
  SkMatrix sk_matrix;
  for (int i = 0; i < 9; ++i) {
    sk_matrix[i] = SafeNarrow(matrix4[kSkMatrixIndexToMatrix4Index[i]]);
  }
  return sk_matrix;
}
}  // namespace

// Pimpl implementation holding actual Flutter types
struct PathImpl {
  SkPath sk_path;
  mutable std::optional<const flutter::DlPath> dl_path;

  PathImpl() = default;

  // Reset cached DlPath whenever SkPath is mutated
  void ResetVolatility() { dl_path.reset(); }
};

// -- Constructor / Destructor --

PathBridge::PathBridge() {
  impl_ = new PathImpl();
}

PathBridge::~PathBridge() {
  delete impl_;
}

// -- Static factory methods --

PathBridge* PathBridge::Clone(const PathBridge* source) {
  auto* clone = new PathBridge();
  // Per Skia docs, this creates a fast COW copy
  clone->impl_->sk_path = source->impl_->sk_path;
  return clone;
}

// -- Fill type --

int PathBridge::GetFillType() const {
  return static_cast<int>(impl_->sk_path.getFillType());
}

void PathBridge::SetFillType(int fill_type) {
  impl_->sk_path.setFillType(static_cast<SkPathFillType>(fill_type));
  impl_->ResetVolatility();
}

// -- Path construction --

void PathBridge::MoveTo(double x, double y) {
  impl_->sk_path.moveTo(SafeNarrow(x), SafeNarrow(y));
  impl_->ResetVolatility();
}

void PathBridge::RelativeMoveTo(double dx, double dy) {
  impl_->sk_path.rMoveTo(SafeNarrow(dx), SafeNarrow(dy));
  impl_->ResetVolatility();
}

void PathBridge::LineTo(double x, double y) {
  impl_->sk_path.lineTo(SafeNarrow(x), SafeNarrow(y));
  impl_->ResetVolatility();
}

void PathBridge::RelativeLineTo(double dx, double dy) {
  impl_->sk_path.rLineTo(SafeNarrow(dx), SafeNarrow(dy));
  impl_->ResetVolatility();
}

void PathBridge::QuadraticBezierTo(double x1, double y1, double x2, double y2) {
  impl_->sk_path.quadTo(SafeNarrow(x1), SafeNarrow(y1),
                         SafeNarrow(x2), SafeNarrow(y2));
  impl_->ResetVolatility();
}

void PathBridge::RelativeQuadraticBezierTo(double x1, double y1,
                                            double x2, double y2) {
  impl_->sk_path.rQuadTo(SafeNarrow(x1), SafeNarrow(y1),
                          SafeNarrow(x2), SafeNarrow(y2));
  impl_->ResetVolatility();
}

void PathBridge::CubicTo(double x1, double y1, double x2, double y2,
                          double x3, double y3) {
  impl_->sk_path.cubicTo(SafeNarrow(x1), SafeNarrow(y1),
                          SafeNarrow(x2), SafeNarrow(y2),
                          SafeNarrow(x3), SafeNarrow(y3));
  impl_->ResetVolatility();
}

void PathBridge::RelativeCubicTo(double x1, double y1, double x2, double y2,
                                  double x3, double y3) {
  impl_->sk_path.rCubicTo(SafeNarrow(x1), SafeNarrow(y1),
                           SafeNarrow(x2), SafeNarrow(y2),
                           SafeNarrow(x3), SafeNarrow(y3));
  impl_->ResetVolatility();
}

void PathBridge::ConicTo(double x1, double y1, double x2, double y2, double w) {
  impl_->sk_path.conicTo(SafeNarrow(x1), SafeNarrow(y1),
                          SafeNarrow(x2), SafeNarrow(y2),
                          SafeNarrow(w));
  impl_->ResetVolatility();
}

void PathBridge::RelativeConicTo(double x1, double y1, double x2, double y2,
                                  double w) {
  impl_->sk_path.rConicTo(SafeNarrow(x1), SafeNarrow(y1),
                           SafeNarrow(x2), SafeNarrow(y2),
                           SafeNarrow(w));
  impl_->ResetVolatility();
}

void PathBridge::ArcTo(double left, double top, double right, double bottom,
                        double start_angle, double sweep_angle,
                        bool force_move_to) {
  // Same as path.cc: convert radians to degrees for Skia
  impl_->sk_path.arcTo(
      SkRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                        SafeNarrow(right), SafeNarrow(bottom)),
      SafeNarrow(start_angle) * 180.0f / static_cast<float>(M_PI),
      SafeNarrow(sweep_angle) * 180.0f / static_cast<float>(M_PI),
      force_move_to);
  impl_->ResetVolatility();
}

void PathBridge::ArcToPoint(double arc_end_x, double arc_end_y,
                             double radius_x, double radius_y,
                             double rotation, bool large_arc,
                             bool clockwise) {
  const auto arc_size = large_arc ? SkPath::ArcSize::kLarge_ArcSize
                                  : SkPath::ArcSize::kSmall_ArcSize;
  const auto direction =
      clockwise ? SkPathDirection::kCW : SkPathDirection::kCCW;

  impl_->sk_path.arcTo(SafeNarrow(radius_x), SafeNarrow(radius_y),
                        SafeNarrow(rotation), arc_size, direction,
                        SafeNarrow(arc_end_x), SafeNarrow(arc_end_y));
  impl_->ResetVolatility();
}

void PathBridge::RelativeArcToPoint(double arc_end_dx, double arc_end_dy,
                                     double radius_x, double radius_y,
                                     double rotation, bool large_arc,
                                     bool clockwise) {
  const auto arc_size = large_arc ? SkPath::ArcSize::kLarge_ArcSize
                                  : SkPath::ArcSize::kSmall_ArcSize;
  const auto direction =
      clockwise ? SkPathDirection::kCW : SkPathDirection::kCCW;
  impl_->sk_path.rArcTo(SafeNarrow(radius_x), SafeNarrow(radius_y),
                         SafeNarrow(rotation), arc_size, direction,
                         SafeNarrow(arc_end_dx), SafeNarrow(arc_end_dy));
  impl_->ResetVolatility();
}

// -- Shape addition --

void PathBridge::AddRect(double left, double top, double right, double bottom) {
  impl_->sk_path.addRect(SkRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                            SafeNarrow(right), SafeNarrow(bottom)));
  impl_->ResetVolatility();
}

void PathBridge::AddOval(double left, double top, double right, double bottom) {
  impl_->sk_path.addOval(SkRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                            SafeNarrow(right), SafeNarrow(bottom)));
  impl_->ResetVolatility();
}

void PathBridge::AddArc(double left, double top, double right, double bottom,
                         double start_angle, double sweep_angle) {
  // Same as path.cc: convert radians to degrees for Skia
  impl_->sk_path.addArc(
      SkRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                        SafeNarrow(right), SafeNarrow(bottom)),
      SafeNarrow(start_angle) * 180.0f / static_cast<float>(M_PI),
      SafeNarrow(sweep_angle) * 180.0f / static_cast<float>(M_PI));
  impl_->ResetVolatility();
}

void PathBridge::AddPolygon(const float* points, int point_count, bool close) {
  // points is [x0, y0, x1, y1, ...], reinterpret as SkPoint array
  impl_->sk_path.addPoly(reinterpret_cast<const SkPoint*>(points),
                          point_count, close);
  impl_->ResetVolatility();
}

void PathBridge::AddRRect(const float* rrect_values) {
  // rrect_values layout: [left, top, right, bottom,
  //   tlRadiusX, tlRadiusY, trRadiusX, trRadiusY,
  //   brRadiusX, brRadiusY, blRadiusX, blRadiusY]
  SkRRect sk_rrect;
  SkRect rect = SkRect::MakeLTRB(rrect_values[0], rrect_values[1],
                                  rrect_values[2], rrect_values[3]);
  SkVector radii[4] = {
      {rrect_values[4], rrect_values[5]},   // top-left
      {rrect_values[6], rrect_values[7]},   // top-right
      {rrect_values[8], rrect_values[9]},   // bottom-right
      {rrect_values[10], rrect_values[11]}, // bottom-left
  };
  sk_rrect.setRectRadii(rect, radii);
  impl_->sk_path.addRRect(sk_rrect);
  impl_->ResetVolatility();
}

void PathBridge::AddRSuperellipse(double left, double top,
                                   double right, double bottom,
                                   double tl_rx, double tl_ry,
                                   double tr_rx, double tr_ry,
                                   double br_rx, double br_ry,
                                   double bl_rx, double bl_ry) {
  // Same as path.cc: build RSuperellipse from bounds and radii,
  // then use DlPathBuilder to construct the path
  flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
      SafeNarrow(left), SafeNarrow(top),
      SafeNarrow(right), SafeNarrow(bottom)).GetPositive();

  impeller::RoundingRadii radii{
      .top_left = flutter::DlSize(SafeNarrow(tl_rx), SafeNarrow(tl_ry)),
      .top_right = flutter::DlSize(SafeNarrow(tr_rx), SafeNarrow(tr_ry)),
      .bottom_left = flutter::DlSize(SafeNarrow(bl_rx), SafeNarrow(bl_ry)),
      .bottom_right = flutter::DlSize(SafeNarrow(br_rx), SafeNarrow(br_ry)),
  };

  flutter::DlPathBuilder builder;
  builder.AddRoundSuperellipse(
      flutter::DlRoundSuperellipse::MakeRectRadii(bounds, radii));
  impl_->sk_path.addPath(builder.TakePath().GetSkPath(),
                          SkPath::kAppend_AddPathMode);
  impl_->ResetVolatility();
}

// -- Path combination --

void PathBridge::AddPath(const PathBridge* path, double dx, double dy) {
  if (!path) {
    return;
  }
  impl_->sk_path.addPath(path->impl_->sk_path,
                          SafeNarrow(dx), SafeNarrow(dy),
                          SkPath::kAppend_AddPathMode);
  impl_->ResetVolatility();
}

void PathBridge::AddPathWithMatrix(const PathBridge* path, double dx, double dy,
                                    const double* matrix4) {
  if (!path) {
    return;
  }
  SkMatrix matrix = ToSkMatrix(matrix4);
  matrix.setTranslateX(matrix.getTranslateX() + SafeNarrow(dx));
  matrix.setTranslateY(matrix.getTranslateY() + SafeNarrow(dy));
  impl_->sk_path.addPath(path->impl_->sk_path, matrix,
                          SkPath::kAppend_AddPathMode);
  impl_->ResetVolatility();
}

void PathBridge::ExtendWithPath(const PathBridge* path, double dx, double dy) {
  if (!path) {
    return;
  }
  impl_->sk_path.addPath(path->impl_->sk_path,
                          SafeNarrow(dx), SafeNarrow(dy),
                          SkPath::kExtend_AddPathMode);
  impl_->ResetVolatility();
}

void PathBridge::ExtendWithPathAndMatrix(const PathBridge* path,
                                          double dx, double dy,
                                          const double* matrix4) {
  if (!path) {
    return;
  }
  SkMatrix matrix = ToSkMatrix(matrix4);
  matrix.setTranslateX(matrix.getTranslateX() + SafeNarrow(dx));
  matrix.setTranslateY(matrix.getTranslateY() + SafeNarrow(dy));
  impl_->sk_path.addPath(path->impl_->sk_path, matrix,
                          SkPath::kExtend_AddPathMode);
  impl_->ResetVolatility();
}

// -- Path operations --

void PathBridge::Close() {
  impl_->sk_path.close();
  impl_->ResetVolatility();
}

void PathBridge::Reset() {
  impl_->sk_path.reset();
  impl_->ResetVolatility();
}

bool PathBridge::Contains(double x, double y) const {
  return impl_->sk_path.contains(SafeNarrow(x), SafeNarrow(y));
}

PathBridge* PathBridge::Shift(const PathBridge* source, double dx, double dy) {
  auto* result = new PathBridge();
  source->impl_->sk_path.offset(SafeNarrow(dx), SafeNarrow(dy),
                                  &result->impl_->sk_path);
  return result;
}

PathBridge* PathBridge::Transform(const PathBridge* source,
                                   const double* matrix4) {
  auto* result = new PathBridge();
  auto sk_matrix = ToSkMatrix(matrix4);
  source->impl_->sk_path.transform(sk_matrix, &result->impl_->sk_path);
  return result;
}

void PathBridge::GetBounds(float* out_bounds) const {
  const SkRect& bounds = impl_->sk_path.getBounds();
  out_bounds[0] = bounds.left();
  out_bounds[1] = bounds.top();
  out_bounds[2] = bounds.right();
  out_bounds[3] = bounds.bottom();
}

bool PathBridge::Op(const PathBridge* path1, const PathBridge* path2,
                     int operation) {
  bool result = ::Op(path1->impl_->sk_path, path2->impl_->sk_path,
                     static_cast<SkPathOp>(operation), &impl_->sk_path);
  impl_->ResetVolatility();
  return result;
}

const void* PathBridge::GetSkPathPtr() const {
  return &impl_->sk_path;
}

void PathBridge::SetFromSkPathPtr(const void* sk_path_ptr) {
  impl_->sk_path = *static_cast<const SkPath*>(sk_path_ptr);
  impl_->ResetVolatility();
}

}  // namespace flutter::swift_bridge
