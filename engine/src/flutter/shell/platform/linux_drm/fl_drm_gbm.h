// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_GBM_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_GBM_H_

#include <gbm.h>
#include <stdint.h>

#include <vector>

namespace flutter {

// Owns the GBM device and one scanout surface per output. Surfaces are
// destroyed with the device; callers hold non-owning handles.
class FlDrmGbm {
 public:
  FlDrmGbm();
  ~FlDrmGbm();

  // Create the GBM device from the DRM fd.
  bool Initialize(int drm_fd);

  // Create a scanout-capable surface (one per output). Returns nullptr on
  // failure. The surface is owned by this class.
  gbm_surface* CreateScanoutSurface(uint32_t width, uint32_t height);

  // Destroy one surface early (hotplug-removed output). The EGL surface on
  // top of it must already be gone.
  void DestroySurface(gbm_surface* surface);

  gbm_device* device() const { return device_; }

 private:
  gbm_device* device_ = nullptr;
  std::vector<gbm_surface*> surfaces_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_GBM_H_
