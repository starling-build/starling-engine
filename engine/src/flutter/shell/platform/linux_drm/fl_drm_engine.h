// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_ENGINE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_ENGINE_H_

#include <stdint.h>

#include "embedder.h"

namespace flutter {

// Utility to send window metrics to the Flutter engine.
void SendWindowMetrics(FlutterEngine engine,
                       uint32_t width, uint32_t height,
                       double pixel_ratio);

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_ENGINE_H_
