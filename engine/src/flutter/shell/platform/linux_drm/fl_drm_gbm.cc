// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_gbm.h"

#include <stdio.h>

namespace flutter {

FlDrmGbm::FlDrmGbm() = default;

FlDrmGbm::~FlDrmGbm() {
  for (gbm_surface* surface : surfaces_) {
    gbm_surface_destroy(surface);
  }
  surfaces_.clear();
  if (device_) {
    gbm_device_destroy(device_);
    device_ = nullptr;
  }
}

bool FlDrmGbm::Initialize(int drm_fd) {
  device_ = gbm_create_device(drm_fd);
  if (!device_) {
    fprintf(stderr, "[GBM] gbm_create_device failed\n");
    return false;
  }
  return true;
}

gbm_surface* FlDrmGbm::CreateScanoutSurface(uint32_t width, uint32_t height) {
  if (!device_) {
    return nullptr;
  }
  gbm_surface* surface =
      gbm_surface_create(device_, width, height, GBM_FORMAT_XRGB8888,
                         GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!surface) {
    fprintf(stderr, "[GBM] gbm_surface_create %ux%u failed\n", width, height);
    return nullptr;
  }
  surfaces_.push_back(surface);
  fprintf(stderr, "[GBM] Surface created: %ux%u\n", width, height);
  return surface;
}

void FlDrmGbm::DestroySurface(gbm_surface* surface) {
  if (!surface) {
    return;
  }
  for (size_t i = 0; i < surfaces_.size(); i++) {
    if (surfaces_[i] == surface) {
      surfaces_.erase(surfaces_.begin() + i);
      gbm_surface_destroy(surface);
      return;
    }
  }
}

}  // namespace flutter
