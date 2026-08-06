// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_CANVAS_BRIDGE_H_
#define FLUTTER_SWIFT_CANVAS_BRIDGE_H_

#include "swift_bridge_export.h"

#include <string>
#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class CanvasBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainCanvasBridge(flutter::swift_bridge::CanvasBridge* p) noexcept;
void ReleaseCanvasBridge(flutter::swift_bridge::CanvasBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl and bridge types
struct CanvasImpl;
class PathBridge;
class ImageBridge;
class VerticesBridge;
class GradientBridge;
class ImageShaderBridge;
class FragmentShaderBridge;
class ColorFilterBridge;
class ImageFilterBridge;

/// C++ bridge wrapping Flutter's _NativeCanvas class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:6559-7281`
/// **Original Name:** `_NativeCanvas`
///
/// This is the concrete Canvas implementation that wraps a DisplayListBuilder.
/// It records graphical operations that are replayed when the associated
/// Picture is rendered.
///
/// Architecture:
/// - Swift NativeCanvas class wraps CanvasBridge
/// - CanvasBridge holds DisplayListBuilder via pimpl pattern
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - Paint data is passed as raw bytes (68-byte buffer matching paint.cc format)
///   plus optional shader/colorFilter/imageFilter bridge pointers
class FLUTTER_SWIFT_BRIDGE_EXPORT
    SWIFT_SHARED_REFERENCE(RetainCanvasBridge, ReleaseCanvasBridge)
        CanvasBridge
    : public IntrusiveReferenceCounted<CanvasBridge> {
 public:
  /// Creates a CanvasBridge with a DisplayListBuilder.
  ///
  /// **Dart Source:** `painting.dart:6560-6568`
  /// **Original:** `_NativeCanvas(PictureRecorder recorder, [Rect? cullRect])`
  ///
  /// @param left Left bound of the cull rect
  /// @param top Top bound of the cull rect
  /// @param right Right bound of the cull rect
  /// @param bottom Bottom bound of the cull rect
  CanvasBridge(double left, double top, double right, double bottom);

  ~CanvasBridge();

  // MARK: - Save/Restore Stack

  /// Saves the current transform and clip on the save stack.
  ///
  /// **Dart Source:** `painting.dart:6586-6588`
  void Save();

  /// Saves a layer with the given paint and optional bounds.
  ///
  /// **Dart Source:** `painting.dart:6603-6609`
  ///
  /// @param has_bounds Whether bounds are provided (false = unbounded)
  /// @param left Left bound (ignored if has_bounds is false)
  /// @param top Top bound
  /// @param right Right bound
  /// @param bottom Bottom bound
  /// @param paint_data Pointer to 68-byte paint data buffer
  /// @param shader_ptr Optional shader bridge (GradientBridge*, ImageShaderBridge*, etc.)
  /// @param color_filter_ptr Optional ColorFilterBridge*
  /// @param image_filter_ptr Optional ImageFilterBridge*
  void SaveLayer(bool has_bounds,
                 double left,
                 double top,
                 double right,
                 double bottom,
                 const void* paint_data,
                 const void* shader_ptr,
                 const void* color_filter_ptr,
                 const void* image_filter_ptr);

  /// Pops the current save stack.
  ///
  /// **Dart Source:** `painting.dart:6627-6629`
  void Restore();

  /// Restores to a previous save level.
  ///
  /// **Dart Source:** `painting.dart:6631-6633`
  void RestoreToCount(int count);

  /// Returns the number of items on the save stack.
  ///
  /// **Dart Source:** `painting.dart:6635-6637`
  int GetSaveCount();

  // MARK: - Transform

  /// Translates the coordinate system.
  ///
  /// **Dart Source:** `painting.dart:6639-6641`
  void Translate(double dx, double dy);

  /// Scales the coordinate system.
  ///
  /// **Dart Source:** `painting.dart:6644-6647`
  void Scale(double sx, double sy);

  /// Rotates the coordinate system by radians.
  ///
  /// **Dart Source:** `painting.dart:6649-6651`
  void Rotate(double radians);

  /// Skews the coordinate system.
  ///
  /// **Dart Source:** `painting.dart:6653-6655`
  void Skew(double sx, double sy);

  /// Transforms by a 4x4 matrix (16 doubles, column-major order).
  ///
  /// **Dart Source:** `painting.dart:6657-6666`
  void Transform(const double* matrix4);

  /// Gets the current 4x4 transform matrix.
  ///
  /// **Dart Source:** `painting.dart:6668-6676`
  /// @param out_matrix4 Output buffer for 16 doubles (column-major)
  void GetTransform(double* out_matrix4);

  // MARK: - Clip

  /// Clips to a rectangle.
  ///
  /// **Dart Source:** `painting.dart:6678-6699`
  void ClipRect(double left,
                double top,
                double right,
                double bottom,
                int clip_op,
                bool do_anti_alias);

  /// Clips to a rounded rectangle.
  ///
  /// **Dart Source:** `painting.dart:6701-6708`
  /// @param rrect_data Pointer to 12 floats (left, top, right, bottom, 8 radii)
  void ClipRRect(const float* rrect_data, bool do_anti_alias);

  /// Clips to a rounded superellipse.
  ///
  /// **Dart Source:** `painting.dart:6710-6717`
  /// @param left/top/right/bottom Bounds of the superellipse
  /// @param tl_radius_x/y through bl_radius_x/y Corner radii
  void ClipRSuperellipse(double left,
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
                         bool do_anti_alias);

  /// Clips to a path.
  ///
  /// **Dart Source:** `painting.dart:6719-6725`
  void ClipPath(const PathBridge* path, bool do_anti_alias);

  /// Gets the local clip bounds.
  ///
  /// **Dart Source:** `painting.dart:6727-6735`
  /// @param out_bounds Output buffer for 4 doubles (left, top, right, bottom)
  void GetLocalClipBounds(double* out_bounds);

  /// Gets the destination clip bounds.
  ///
  /// **Dart Source:** `painting.dart:6737-6745`
  /// @param out_bounds Output buffer for 4 doubles (left, top, right, bottom)
  void GetDestinationClipBounds(double* out_bounds);

  // MARK: - Drawing Methods

  /// Draws a solid color with the given blend mode.
  ///
  /// **Dart Source:** `painting.dart:6747-6753`
  void DrawColor(int color, int blend_mode);

  /// Draws a line between two points.
  ///
  /// **Dart Source:** `painting.dart:6755-6772`
  void DrawLine(double x1,
                double y1,
                double x2,
                double y2,
                const void* paint_data,
                const void* shader_ptr,
                const void* color_filter_ptr,
                const void* image_filter_ptr);

  /// Fills the canvas with paint.
  ///
  /// **Dart Source:** `painting.dart:6774-6780`
  void DrawPaint(const void* paint_data,
                 const void* shader_ptr,
                 const void* color_filter_ptr,
                 const void* image_filter_ptr);

  /// Draws a rectangle.
  ///
  /// **Dart Source:** `painting.dart:6782-6801`
  void DrawRect(double left,
                double top,
                double right,
                double bottom,
                const void* paint_data,
                const void* shader_ptr,
                const void* color_filter_ptr,
                const void* image_filter_ptr);

  /// Draws a rounded rectangle.
  ///
  /// **Dart Source:** `painting.dart:6803-6810`
  /// @param rrect_data Pointer to 12 floats (RRect layout)
  void DrawRRect(const float* rrect_data,
                 const void* paint_data,
                 const void* shader_ptr,
                 const void* color_filter_ptr,
                 const void* image_filter_ptr);

  /// Draws a difference of two rounded rectangles.
  ///
  /// **Dart Source:** `painting.dart:6812-6827`
  void DrawDRRect(const float* outer_data,
                  const float* inner_data,
                  const void* paint_data,
                  const void* shader_ptr,
                  const void* color_filter_ptr,
                  const void* image_filter_ptr);

  /// Draws a rounded superellipse.
  ///
  /// **Dart Source:** `painting.dart:6829-6842`
  void DrawRSuperellipse(double left,
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
                         const void* image_filter_ptr);

  /// Draws an oval inscribed in the given rectangle.
  ///
  /// **Dart Source:** `painting.dart:6844-6863`
  void DrawOval(double left,
                double top,
                double right,
                double bottom,
                const void* paint_data,
                const void* shader_ptr,
                const void* color_filter_ptr,
                const void* image_filter_ptr);

  /// Draws a circle.
  ///
  /// **Dart Source:** `painting.dart:6865-6880`
  void DrawCircle(double x,
                  double y,
                  double radius,
                  const void* paint_data,
                  const void* shader_ptr,
                  const void* color_filter_ptr,
                  const void* image_filter_ptr);

  /// Draws an arc.
  ///
  /// **Dart Source:** `painting.dart:6882-6922`
  void DrawArc(double left,
               double top,
               double right,
               double bottom,
               double start_angle,
               double sweep_angle,
               bool use_center,
               const void* paint_data,
               const void* shader_ptr,
               const void* color_filter_ptr,
               const void* image_filter_ptr);

  /// Draws a path.
  ///
  /// **Dart Source:** `painting.dart:6924-6930`
  void DrawPath(const PathBridge* path,
                const void* paint_data,
                const void* shader_ptr,
                const void* color_filter_ptr,
                const void* image_filter_ptr);

  /// Draws an image at the given position.
  ///
  /// **Dart Source:** `painting.dart:6932-6959`
  /// @returns Error string if image is invalid, empty string on success
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const char* DrawImage(const ImageBridge* image,
                        double x,
                        double y,
                        const void* paint_data,
                        const void* shader_ptr,
                        const void* color_filter_ptr,
                        const void* image_filter_ptr,
                        int filter_quality_index);

  /// Draws a subset of an image into a destination rectangle.
  ///
  /// **Dart Source:** `painting.dart:6961-7015`
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const char* DrawImageRect(const ImageBridge* image,
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
                            int filter_quality_index);

  /// Draws an image using nine-patch scaling.
  ///
  /// **Dart Source:** `painting.dart:7017-7071`
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const char* DrawImageNine(const ImageBridge* image,
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
                            int filter_quality_index);

  /// Draws a picture (display list).
  ///
  /// **Dart Source:** `painting.dart:7073-7080`
  /// @param display_list_ptr Pointer to the sk_sp<DisplayList> (from PictureBridge)
  void DrawDisplayList(const void* display_list_ptr);

  /// Draws points/lines/polygon.
  ///
  /// **Dart Source:** `painting.dart:7091-7110`
  /// @param point_mode 0=points, 1=lines, 2=polygon
  /// @param points Float array of x,y pairs
  /// @param point_count Number of floats in the points array
  void DrawPoints(int point_mode,
                  const float* points,
                  int point_count,
                  const void* paint_data,
                  const void* shader_ptr,
                  const void* color_filter_ptr,
                  const void* image_filter_ptr);

  /// Draws vertices as triangles.
  ///
  /// **Dart Source:** `painting.dart:7112-7126`
  void DrawVertices(const VerticesBridge* vertices,
                    int blend_mode,
                    const void* paint_data,
                    const void* shader_ptr,
                    const void* color_filter_ptr,
                    const void* image_filter_ptr);

  /// Draws an atlas (image with multiple source rects and transforms).
  ///
  /// **Dart Source:** `painting.dart:7128-7262`
  /// @param rst_transforms Float array of RSTransform data (scos, ssin, tx, ty per entry)
  /// @param rects Float array of rect data (left, top, right, bottom per entry)
  /// @param rect_count Number of rects (must match number of transforms)
  /// @param colors Optional int32 array of colors (one per rect), or nullptr
  /// @param color_count Number of colors (0 if colors is nullptr)
  /// @param blend_mode Blend mode for colors
  /// @param cull_rect Optional cull rect (4 floats), or nullptr
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const char* DrawAtlas(const ImageBridge* atlas,
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
                        int filter_quality_index);

  /// Draws a shadow for the given path.
  ///
  /// **Dart Source:** `painting.dart:7264-7277`
  void DrawShadow(const PathBridge* path,
                  int color,
                  double elevation,
                  bool transparent_occluder);

  /// Invalidates the canvas (called when recording ends).
  ///
  /// **Dart Source:** `painting.dart:7279-7281` (implicit via _recorder = null)
  void Invalidate();

  /// Returns a pointer to the internal DisplayListBuilder (for PictureRecorder).
  ///
  /// Returns nullptr if the canvas has been invalidated.
  const void* GetDisplayListBuilderPtr() const;

 private:
  CanvasBridge(const CanvasBridge&) = delete;
  CanvasBridge& operator=(const CanvasBridge&) = delete;

  // Pimpl - holds the actual Flutter DisplayListBuilder
  CanvasImpl* impl_;

  // Internal error message buffer
  mutable std::string error_message_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainCanvasBridge(
    flutter::swift_bridge::CanvasBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseCanvasBridge(
    flutter::swift_bridge::CanvasBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_CANVAS_BRIDGE_H_
