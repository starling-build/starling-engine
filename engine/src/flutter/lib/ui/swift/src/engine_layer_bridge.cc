// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/engine_layer_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/flow/layers/container_layer.h"

namespace flutter::swift_bridge {

// Pimpl implementation holding the actual Flutter types.
struct EngineLayerImpl {
  std::shared_ptr<flutter::ContainerLayer> layer;

  explicit EngineLayerImpl(std::shared_ptr<flutter::ContainerLayer> l)
      : layer(std::move(l)) {}
};

EngineLayerBridge::EngineLayerBridge(const void* layer_ptr) {
  if (layer_ptr) {
    const auto& layer =
        *static_cast<const std::shared_ptr<flutter::ContainerLayer>*>(
            layer_ptr);
    impl_ = new EngineLayerImpl(layer);
  } else {
    impl_ = new EngineLayerImpl(nullptr);
  }
}

EngineLayerBridge::~EngineLayerBridge() {
  delete impl_;
}

void EngineLayerBridge::Dispose() {
  if (impl_) {
    impl_->layer.reset();
  }
}

bool EngineLayerBridge::IsDisposed() const {
  return !impl_ || !impl_->layer;
}

const void* EngineLayerBridge::GetLayerPtr() const {
  if (!impl_ || !impl_->layer) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->layer);
}

}  // namespace flutter::swift_bridge
