// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_PATH_MEASURE_BRIDGE_H_
#define FLUTTER_SWIFT_PATH_MEASURE_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>
#include "intrusive_reference_counted.h"
#include "path_bridge.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class PathMeasureBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainPathMeasureBridge(flutter::swift_bridge::PathMeasureBridge* p) noexcept;
void ReleasePathMeasureBridge(flutter::swift_bridge::PathMeasureBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct PathMeasureImpl;

/// C++ bridge wrapping Flutter's _PathMeasure (CanvasPathMeasure) class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:3676-3763`
///
/// PathMeasureBridge wraps Skia's SkContourMeasureIter to provide
/// path measurement functionality including contour length, position/tangent
/// at offset, segment extraction, and contour iteration.
///
/// Architecture:
/// - Swift PathMetrics/PathMetricIterator/PathMetric wrap PathMeasureBridge
/// - PathMeasureBridge holds SkContourMeasureIter via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainPathMeasureBridge, ReleasePathMeasureBridge)
        PathMeasureBridge
    : public IntrusiveReferenceCounted<PathMeasureBridge> {
 public:
  /// Creates a PathMeasureBridge from a PathBridge and forceClosed flag.
  ///
  /// **Dart Source:** `painting.dart:3677-3678` (_PathMeasure constructor)
  PathMeasureBridge(const PathBridge* path, bool force_closed);

  /// Gets the length of a specific contour.
  ///
  /// **Dart Source:** `painting.dart:3684-3690` (_PathMeasure.length)
  /// @param contour_index The zero-based contour index
  /// @return The length of the contour, or -1 if index is out of range
  double GetLength(int contour_index) const;

  /// Gets position and tangent at a given distance along a contour.
  ///
  /// **Dart Source:** `painting.dart:3695-3707` (_PathMeasure.getTangentForOffset)
  /// @param contour_index The zero-based contour index
  /// @param distance Distance along the contour
  /// @param out_values Array of 5 floats: [success, pos_x, pos_y, tan_x, tan_y]
  ///        success == 1.0 if valid, 0.0 if not
  void GetPosTan(int contour_index, double distance, float* out_values) const;

  /// Extracts a segment from a contour into a new PathBridge.
  ///
  /// **Dart Source:** `painting.dart:3712-3719` (_PathMeasure.extractPath)
  /// @param contour_index The zero-based contour index
  /// @param start Start distance
  /// @param end End distance
  /// @param start_with_move_to Whether to begin with a moveTo
  /// @return New PathBridge containing the segment, or empty path on failure
  static PathBridge* ExtractPath(const PathMeasureBridge* measure,
                                 int contour_index,
                                 double start,
                                 double end,
                                 bool start_with_move_to);

  /// Checks if a specific contour is closed.
  ///
  /// **Dart Source:** `painting.dart:3733-3738` (_PathMeasure.isClosed)
  /// @param contour_index The zero-based contour index
  /// @return true if the contour is closed, false otherwise
  bool IsClosed(int contour_index) const;

  /// Advances to the next contour in the path.
  ///
  /// **Dart Source:** `painting.dart:3748-3754` (_PathMeasure._nextContour)
  /// @return true if a next contour exists, false if no more contours
  bool NextContour();

  ~PathMeasureBridge();

 private:
  PathMeasureBridge(const PathMeasureBridge&) = delete;
  PathMeasureBridge& operator=(const PathMeasureBridge&) = delete;

  // Pimpl - holds the actual Flutter/Skia types
  PathMeasureImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainPathMeasureBridge(
    flutter::swift_bridge::PathMeasureBridge* p) noexcept {
  p->Retain();
}
inline void ReleasePathMeasureBridge(
    flutter::swift_bridge::PathMeasureBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_PATH_MEASURE_BRIDGE_H_
