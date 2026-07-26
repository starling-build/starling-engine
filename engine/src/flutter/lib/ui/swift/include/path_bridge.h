// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_PATH_BRIDGE_H_
#define FLUTTER_SWIFT_PATH_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class PathBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainPathBridge(flutter::swift_bridge::PathBridge* p) noexcept;
void ReleasePathBridge(flutter::swift_bridge::PathBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declaration for pimpl
struct PathImpl;

/// C++ bridge wrapping Flutter's Path class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:2837-3494`
///
/// Path is a complex, one-dimensional subset of a plane composed of
/// sub-paths containing lines, arcs, and bezier curves. It wraps the
/// underlying SkPath and DlPath objects.
///
/// Architecture:
/// - Swift Path class wraps PathBridge
/// - PathBridge holds SkPath via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainPathBridge, ReleasePathBridge)
        PathBridge
    : public IntrusiveReferenceCounted<PathBridge> {
 public:
  /// Creates a new empty PathBridge.
  ///
  /// **Dart Source:** `painting.dart:2857` (Path() factory)
  PathBridge();

  /// Creates a PathBridge by cloning another PathBridge.
  ///
  /// **Dart Source:** `painting.dart:2859-2867` (Path.from factory)
  /// This copy is fast (Skia COW) and does not require additional memory
  /// unless either path is modified.
  static PathBridge* Clone(const PathBridge* source);

  // -- Fill type --

  /// Gets the path fill type.
  ///
  /// **Dart Source:** `painting.dart:2869-2872`
  /// Returns: 0 = nonZero, 1 = evenOdd
  int GetFillType() const;

  /// Sets the path fill type.
  ///
  /// **Dart Source:** `painting.dart:2873`
  /// @param fill_type 0 = nonZero, 1 = evenOdd
  void SetFillType(int fill_type);

  // -- Path construction --

  /// Starts a new sub-path at the given coordinate.
  ///
  /// **Dart Source:** `painting.dart:2875-2876`
  void MoveTo(double x, double y);

  /// Starts a new sub-path at the given offset from the current point.
  ///
  /// **Dart Source:** `painting.dart:2878-2879`
  void RelativeMoveTo(double dx, double dy);

  /// Adds a straight line segment from the current point to the given point.
  ///
  /// **Dart Source:** `painting.dart:2881-2883`
  void LineTo(double x, double y);

  /// Adds a straight line segment from the current point to the point at
  /// the given offset from the current point.
  ///
  /// **Dart Source:** `painting.dart:2885-2887`
  void RelativeLineTo(double dx, double dy);

  /// Adds a quadratic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2889-2895`
  void QuadraticBezierTo(double x1, double y1, double x2, double y2);

  /// Adds a relative quadratic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2897-2901`
  void RelativeQuadraticBezierTo(double x1, double y1, double x2, double y2);

  /// Adds a cubic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2903-2909`
  void CubicTo(double x1, double y1, double x2, double y2, double x3, double y3);

  /// Adds a relative cubic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2911-2915`
  void RelativeCubicTo(double x1, double y1, double x2, double y2, double x3, double y3);

  /// Adds a conic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2917-2925`
  void ConicTo(double x1, double y1, double x2, double y2, double w);

  /// Adds a relative conic bezier segment.
  ///
  /// **Dart Source:** `painting.dart:2927-2933`
  void RelativeConicTo(double x1, double y1, double x2, double y2, double w);

  /// Adds an arc segment.
  ///
  /// **Dart Source:** `painting.dart:2935-2951`
  void ArcTo(double left, double top, double right, double bottom,
             double start_angle, double sweep_angle, bool force_move_to);

  /// Appends conic curves to describe an arc to a point.
  ///
  /// **Dart Source:** `painting.dart:2953-2971`
  void ArcToPoint(double arc_end_x, double arc_end_y,
                  double radius_x, double radius_y,
                  double rotation, bool large_arc, bool clockwise);

  /// Appends relative conic curves to describe an arc.
  ///
  /// **Dart Source:** `painting.dart:2973-2993`
  void RelativeArcToPoint(double arc_end_dx, double arc_end_dy,
                          double radius_x, double radius_y,
                          double rotation, bool large_arc, bool clockwise);

  // -- Shape addition --

  /// Adds a rectangle sub-path.
  ///
  /// **Dart Source:** `painting.dart:2995-2997`
  void AddRect(double left, double top, double right, double bottom);

  /// Adds an oval sub-path.
  ///
  /// **Dart Source:** `painting.dart:2999-3004`
  void AddOval(double left, double top, double right, double bottom);

  /// Adds an arc sub-path.
  ///
  /// **Dart Source:** `painting.dart:3006-3020`
  void AddArc(double left, double top, double right, double bottom,
              double start_angle, double sweep_angle);

  /// Adds a polygon sub-path from a flat array of point coordinates.
  ///
  /// **Dart Source:** `painting.dart:3022-3029`
  /// @param points Flat float array [x0, y0, x1, y1, ...]
  /// @param point_count Number of points (half the array length)
  /// @param close Whether to close the polygon
  void AddPolygon(const float* points, int point_count, bool close);

  /// Adds a rounded rectangle sub-path from 12 float values.
  ///
  /// **Dart Source:** `painting.dart:3031-3034`
  /// @param rrect_values Array of 12 floats encoding the RRect:
  ///   [left, top, right, bottom, tlRadiusX, tlRadiusY, trRadiusX, trRadiusY,
  ///    brRadiusX, brRadiusY, blRadiusX, blRadiusY]
  void AddRRect(const float* rrect_values);

  /// Adds a rounded superellipse sub-path.
  ///
  /// **Dart Source:** `painting.dart:3036-3038`
  /// @param left, top, right, bottom: Bounding rectangle
  /// @param tl_rx..bl_ry: Corner radii (topLeft, topRight, bottomRight, bottomLeft)
  void AddRSuperellipse(double left, double top, double right, double bottom,
                         double tl_rx, double tl_ry,
                         double tr_rx, double tr_ry,
                         double br_rx, double br_ry,
                         double bl_rx, double bl_ry);

  // -- Path combination --

  /// Adds the sub-paths of another path, offset by (dx, dy).
  ///
  /// **Dart Source:** `painting.dart:3040-3045`
  void AddPath(const PathBridge* path, double dx, double dy);

  /// Adds the sub-paths of another path with a 4x4 matrix transform, offset by (dx, dy).
  ///
  /// **Dart Source:** `painting.dart:3040-3045` (with matrix4 parameter)
  /// @param matrix4 16 double values in column-major order
  void AddPathWithMatrix(const PathBridge* path, double dx, double dy,
                         const double* matrix4);

  /// Extends the current sub-path with sub-paths of another path.
  ///
  /// **Dart Source:** `painting.dart:3047-3054`
  void ExtendWithPath(const PathBridge* path, double dx, double dy);

  /// Extends the current sub-path with a matrix transform.
  ///
  /// **Dart Source:** `painting.dart:3047-3054` (with matrix4 parameter)
  void ExtendWithPathAndMatrix(const PathBridge* path, double dx, double dy,
                               const double* matrix4);

  // -- Path operations --

  /// Closes the last sub-path.
  ///
  /// **Dart Source:** `painting.dart:3056-3058`
  void Close();

  /// Resets the path to empty state.
  ///
  /// **Dart Source:** `painting.dart:3060-3063`
  void Reset();

  /// Tests if a point is within the path.
  ///
  /// **Dart Source:** `painting.dart:3065-3072`
  bool Contains(double x, double y) const;

  /// Returns a shifted copy of the path.
  ///
  /// **Dart Source:** `painting.dart:3074-3076`
  static PathBridge* Shift(const PathBridge* source, double dx, double dy);

  /// Returns a transformed copy of the path.
  ///
  /// **Dart Source:** `painting.dart:3078-3080`
  /// @param matrix4 16 double values in column-major order
  static PathBridge* Transform(const PathBridge* source, const double* matrix4);

  /// Gets the bounding rectangle of the path.
  ///
  /// **Dart Source:** `painting.dart:3082-3097`
  /// @param out_bounds Array of 4 floats to receive [left, top, right, bottom]
  void GetBounds(float* out_bounds) const;

  /// Combines two paths according to a path operation.
  ///
  /// **Dart Source:** `painting.dart:3099-3113`
  /// @param path1 First path
  /// @param path2 Second path
  /// @param operation PathOperation index (0-4)
  /// @return true if the operation succeeded
  bool Op(const PathBridge* path1, const PathBridge* path2, int operation);

  /// Returns an opaque pointer to the internal SkPath.
  /// Used by PathMeasureBridge to access the underlying path data.
  /// @return Pointer to the internal SkPath (caller must cast to const SkPath*)
  const void* GetSkPathPtr() const;

  /// Sets the internal path from an opaque SkPath pointer.
  /// Used by PathMeasureBridge to set the path from an extracted segment.
  /// @param sk_path_ptr Pointer to an SkPath to copy from
  void SetFromSkPathPtr(const void* sk_path_ptr);

  ~PathBridge();

 private:
  PathBridge(const PathBridge&) = delete;
  PathBridge& operator=(const PathBridge&) = delete;

  // Pimpl - holds the actual Flutter types (SkPath, DlPath)
  PathImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainPathBridge(
    flutter::swift_bridge::PathBridge* p) noexcept {
  p->Retain();
}
inline void ReleasePathBridge(
    flutter::swift_bridge::PathBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_PATH_BRIDGE_H_
