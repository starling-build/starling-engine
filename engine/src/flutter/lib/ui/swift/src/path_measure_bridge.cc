// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/path_measure_bridge.h"

// Flutter engine headers (only in .cc file)
#include "third_party/skia/include/core/SkContourMeasure.h"
#include "third_party/skia/include/core/SkPath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

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
}  // namespace

// Pimpl implementation holding actual Skia types
struct PathMeasureImpl {
  std::unique_ptr<SkContourMeasureIter> path_measure;
  std::vector<sk_sp<SkContourMeasure>> measures;

  PathMeasureImpl() = default;
};

// -- Constructor / Destructor --

PathMeasureBridge::PathMeasureBridge(const PathBridge* path,
                                     bool force_closed) {
  impl_ = new PathMeasureImpl();
  if (path) {
    // Access PathBridge's internal SkPath via opaque pointer accessor
    const SkPath& sk_path = *static_cast<const SkPath*>(path->GetSkPathPtr());
    SkScalar res_scale = 1;
    impl_->path_measure =
        std::make_unique<SkContourMeasureIter>(sk_path, force_closed, res_scale);
  } else {
    impl_->path_measure = std::make_unique<SkContourMeasureIter>();
  }
}

PathMeasureBridge::~PathMeasureBridge() {
  delete impl_;
}

// -- Measurement methods --

double PathMeasureBridge::GetLength(int contour_index) const {
  if (static_cast<size_t>(contour_index) < impl_->measures.size()) {
    return impl_->measures[contour_index]->length();
  }
  return -1;
}

void PathMeasureBridge::GetPosTan(int contour_index, double distance,
                                   float* out_values) const {
  out_values[0] = 0;  // 0 = failure flag (same as Dart convention)

  if (static_cast<size_t>(contour_index) >= impl_->measures.size()) {
    return;
  }

  SkPoint pos;
  SkVector tan;
  float fdistance = SafeNarrow(distance);
  bool success = impl_->measures[contour_index]->getPosTan(fdistance, &pos, &tan);

  if (success) {
    out_values[0] = 1;  // 1 = success flag
    out_values[1] = pos.x();
    out_values[2] = pos.y();
    out_values[3] = tan.x();
    out_values[4] = tan.y();
  }
}

PathBridge* PathMeasureBridge::ExtractPath(const PathMeasureBridge* measure,
                                            int contour_index,
                                            double start,
                                            double end,
                                            bool start_with_move_to) {
  auto* result = new PathBridge();

  if (static_cast<size_t>(contour_index) >= measure->impl_->measures.size()) {
    return result;  // Return empty path
  }

  SkPath dst;
  bool success = measure->impl_->measures[contour_index]->getSegment(
      SafeNarrow(start), SafeNarrow(end), &dst, start_with_move_to);

  if (success) {
    // Copy the extracted path into the result via opaque accessor
    result->SetFromSkPathPtr(&dst);
  }
  return result;
}

bool PathMeasureBridge::IsClosed(int contour_index) const {
  if (static_cast<size_t>(contour_index) < impl_->measures.size()) {
    return impl_->measures[contour_index]->isClosed();
  }
  return false;
}

bool PathMeasureBridge::NextContour() {
  auto measure = impl_->path_measure->next();
  if (measure) {
    impl_->measures.push_back(std::move(measure));
    return true;
  }
  return false;
}

}  // namespace flutter::swift_bridge
