// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/scene_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/flow/layers/layer.h"

#include <memory>

namespace flutter::swift_bridge {

// Pimpl implementation holding the actual Flutter types.
struct SceneImpl {
  std::shared_ptr<flutter::Layer> root_layer;

  explicit SceneImpl(std::shared_ptr<flutter::Layer> layer)
      : root_layer(std::move(layer)) {}
};

SceneBridge::SceneBridge(const void* root_layer_ptr) {
  if (root_layer_ptr) {
    const auto& layer =
        *static_cast<const std::shared_ptr<flutter::Layer>*>(root_layer_ptr);
    impl_ = new SceneImpl(layer);
  } else {
    impl_ = new SceneImpl(nullptr);
  }
}

SceneBridge::~SceneBridge() {
  delete impl_;
}

void SceneBridge::Dispose() {
  if (impl_) {
    impl_->root_layer.reset();
  }
}

bool SceneBridge::IsDisposed() const {
  return !impl_ || !impl_->root_layer;
}

const void* SceneBridge::GetRootLayerPtr() const {
  if (!impl_ || !impl_->root_layer) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->root_layer);
}

}  // namespace flutter::swift_bridge
