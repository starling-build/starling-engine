// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_RSUPERELLIPSE_BRIDGE_H_
#define FLUTTER_SWIFT_RSUPERELLIPSE_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class RSuperellipseBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainRSuperellipseBridge(flutter::swift_bridge::RSuperellipseBridge* p) noexcept;
void ReleaseRSuperellipseBridge(flutter::swift_bridge::RSuperellipseBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct RSuperellipseImpl;

/// C++ bridge wrapping Flutter's RSuperellipse.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/geometry.dart:2051-2110`
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainRSuperellipseBridge, ReleaseRSuperellipseBridge)
        RSuperellipseBridge
    : public IntrusiveReferenceCounted<RSuperellipseBridge> {
 public:
  /// Creates an RSuperellipseBridge wrapping a Flutter RSuperellipse.
  RSuperellipseBridge(double left,
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
                      double bl_radius_y);

  /// Checks if a point is contained within the rounded superellipse.
  bool Contains(double x, double y) const;

  ~RSuperellipseBridge();

 private:
  RSuperellipseBridge(const RSuperellipseBridge&) = delete;
  RSuperellipseBridge& operator=(const RSuperellipseBridge&) = delete;

  // Pimpl - holds the actual Flutter types
  RSuperellipseImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainRSuperellipseBridge(flutter::swift_bridge::RSuperellipseBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseRSuperellipseBridge(flutter::swift_bridge::RSuperellipseBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_RSUPERELLIPSE_BRIDGE_H_
