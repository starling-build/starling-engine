// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/rsuperellipse_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/geometry/dl_geometry_types.h"
#include "flutter/impeller/geometry/round_superellipse_param.h"
#include "flutter/impeller/geometry/rounding_radii.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

// Pimpl implementation holding actual Flutter types
struct RSuperellipseImpl {
  flutter::DlRect bounds;
  impeller::RoundingRadii radii;

  RSuperellipseImpl(flutter::DlRect b, impeller::RoundingRadii r)
      : bounds(b), radii(r) {}
};

RSuperellipseBridge::RSuperellipseBridge(double left,
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
                                         double bl_radius_y) {
  // Build bounds (same as rsuperellipse.cc:15-22)
  flutter::DlRect bounds = flutter::DlRect::MakeLTRB(
      SafeNarrow(left), SafeNarrow(top),
      SafeNarrow(right), SafeNarrow(bottom)).GetPositive();

  // Build radii (same as rsuperellipse.cc:24-40)
  // Note: Flutter uses TL,TR,BR,BL but Impeller uses TL,TR,BL,BR
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

  impl_ = new RSuperellipseImpl(bounds, radii);
}

RSuperellipseBridge::~RSuperellipseBridge() {
  delete impl_;
}

bool RSuperellipseBridge::Contains(double x, double y) const {
  // Same logic as rsuperellipse.cc:83-89
  flutter::DlPoint point(SafeNarrow(x), SafeNarrow(y));

  if (!impl_->bounds.Contains(point)) {
    return false;
  }

  auto param = impeller::RoundSuperellipseParam::MakeBoundsRadii(
      impl_->bounds, impl_->radii);
  return param.Contains(point);
}

}  // namespace flutter::swift_bridge
