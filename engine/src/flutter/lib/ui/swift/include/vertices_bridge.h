// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_VERTICES_BRIDGE_H_
#define FLUTTER_SWIFT_VERTICES_BRIDGE_H_

#include <cstdint>
#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class VerticesBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainVerticesBridge(flutter::swift_bridge::VerticesBridge* p) noexcept;
void ReleaseVerticesBridge(flutter::swift_bridge::VerticesBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct VerticesBridgeImpl;

/// C++ bridge wrapping Flutter's Vertices / DlVertices.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:5446-5657`
/// **Original:** `Vertices` class (extends NativeFieldWrapperClass1)
///
/// This bridge wraps the underlying DlVertices display list object.
/// Swift's Vertices class wraps this bridge for automatic memory management.
///
/// Architecture:
/// - Swift Vertices class wraps VerticesBridge
/// - VerticesBridge wraps the underlying DlVertices
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainVerticesBridge, ReleaseVerticesBridge)
        VerticesBridge
    : public IntrusiveReferenceCounted<VerticesBridge> {
 public:
  /// Creates a VerticesBridge with vertex data.
  ///
  /// **Dart Source:** `painting.dart:5489-5533` and `painting.dart:5579-5614`
  /// **Original:** `Vertices(...)` and `Vertices.raw(...)` constructors
  ///
  /// Parameters:
  /// - mode: DlVertexMode index (0=triangles, 1=triangleStrip, 2=triangleFan)
  /// - positions: Float array of x,y pairs (required)
  /// - position_count: Number of floats in positions array
  /// - texture_coordinates: Float array of x,y pairs (optional, nullptr if not provided)
  /// - texture_coordinate_count: Number of floats in texture_coordinates array
  /// - colors: Int32 array of ARGB colors (optional, nullptr if not provided)
  /// - color_count: Number of colors
  /// - indices: Uint16 array of indices (optional, nullptr if not provided)
  /// - index_count: Number of indices
  ///
  /// Returns false via IsValid() if construction fails.
  VerticesBridge(int mode,
                 const float* positions,
                 int position_count,
                 const float* texture_coordinates,
                 int texture_coordinate_count,
                 const int32_t* colors,
                 int color_count,
                 const uint16_t* indices,
                 int index_count);

  /// Whether the vertices were constructed successfully.
  bool IsValid() const;

  /// Release the resources used by this object.
  ///
  /// **Dart Source:** `painting.dart:5628-5635`
  /// **Original:** `void dispose()`
  void Dispose();

  /// Whether dispose() has been called.
  bool IsDisposed() const;

  /// Returns an opaque pointer to the underlying shared_ptr<DlVertices>.
  ///
  /// This is used by Canvas bridge to access the vertex data for drawing.
  /// Returns nullptr if no vertices are set or disposed.
  const void* GetDlVerticesPtr() const;

  ~VerticesBridge();

 private:
  VerticesBridge(const VerticesBridge&) = delete;
  VerticesBridge& operator=(const VerticesBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlVertices)
  VerticesBridgeImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainVerticesBridge(
    flutter::swift_bridge::VerticesBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseVerticesBridge(
    flutter::swift_bridge::VerticesBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_VERTICES_BRIDGE_H_
