// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_engine.h"

namespace flutter {

void SendWindowMetrics(FlutterEngine engine,
                       uint32_t width, uint32_t height,
                       double pixel_ratio) {
  if (!engine) {
    return;
  }

  FlutterWindowMetricsEvent event = {};
  event.struct_size = sizeof(event);
  event.width = width;
  event.height = height;
  event.pixel_ratio = pixel_ratio;
  event.view_id = 0;

  FlutterEngineSendWindowMetricsEvent(engine, &event);
}

}  // namespace flutter
