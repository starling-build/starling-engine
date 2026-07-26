// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/vertices_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_vertices.h"

#include <memory>

namespace flutter::swift_bridge {

// Pimpl implementation holding actual Flutter types
struct VerticesBridgeImpl {
  std::shared_ptr<DlVertices> vertices;
  bool disposed;

  VerticesBridgeImpl() : vertices(nullptr), disposed(false) {}
};

VerticesBridge::VerticesBridge(int mode,
                               const float* positions,
                               int position_count,
                               const float* texture_coordinates,
                               int texture_coordinate_count,
                               const int32_t* colors,
                               int color_count,
                               const uint16_t* indices,
                               int index_count) {
  impl_ = new VerticesBridgeImpl();

  if (!positions || position_count <= 0) {
    return;
  }

  DlVertexMode vertex_mode = static_cast<DlVertexMode>(mode);
  int vertex_count = position_count / 2;

  // Build flags based on which optional data is provided
  // Same logic as Vertices::init (vertices.cc:38-44)
  DlVertices::Builder::Flags flags;
  if (texture_coordinates && texture_coordinate_count > 0) {
    flags = flags | DlVertices::Builder::kHasTextureCoordinates;
  }
  if (colors && color_count > 0) {
    flags = flags | DlVertices::Builder::kHasColors;
  }

  DlVertices::Builder builder(vertex_mode, vertex_count, flags,
                              (indices && index_count > 0) ? index_count : 0);

  if (!builder.is_valid()) {
    return;
  }

  // Store positions (required) - same as vertices.cc:53
  builder.store_vertices(positions);

  // Store texture coordinates (optional) - same as vertices.cc:55-59
  if (texture_coordinates && texture_coordinate_count > 0) {
    builder.store_texture_coordinates(texture_coordinates);
  }

  // Store colors (optional) - same as vertices.cc:61-65
  // Colors are Int32 ARGB values, cast to SkColor (which is uint32_t)
  if (colors && color_count > 0) {
    builder.store_colors(reinterpret_cast<const uint32_t*>(colors));
  }

  // Store indices (optional) - same as vertices.cc:67-69
  if (indices && index_count > 0) {
    builder.store_indices(indices);
  }

  impl_->vertices = builder.build();
}

VerticesBridge::~VerticesBridge() {
  delete impl_;
}

bool VerticesBridge::IsValid() const {
  return impl_->vertices != nullptr;
}

void VerticesBridge::Dispose() {
  // Match C++ Vertices::dispose() from vertices.cc:83-86
  //   vertices_.reset();
  //   ClearDartWrapper();  // Not needed in Swift (no Dart VM)
  impl_->vertices.reset();
  impl_->disposed = true;
}

bool VerticesBridge::IsDisposed() const {
  return impl_->disposed;
}

const void* VerticesBridge::GetDlVerticesPtr() const {
  if (!impl_->vertices) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->vertices);
}

}  // namespace flutter::swift_bridge
