// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/swift_bridge_engine_registry.h"

#include <cstring>
#include <memory>

// Forward declaration — full header only in .cc files that need it.
namespace txt {
class FontCollection;
}

namespace flutter::swift_bridge {

// Static member definitions.
SwiftBridgeEngineRegistry::RenderCallback
    SwiftBridgeEngineRegistry::render_callback_;
SwiftBridgeEngineRegistry::ScheduleFrameCallback
    SwiftBridgeEngineRegistry::schedule_frame_callback_;
float SwiftBridgeEngineRegistry::device_pixel_ratio_ = 1.0f;
bool SwiftBridgeEngineRegistry::frame_rendered_ = false;
void* SwiftBridgeEngineRegistry::font_collection_storage_ = nullptr;

void SwiftBridgeEngineRegistry::SetRenderCallback(RenderCallback callback) {
  render_callback_ = std::move(callback);
}

const SwiftBridgeEngineRegistry::RenderCallback&
SwiftBridgeEngineRegistry::GetRenderCallback() {
  return render_callback_;
}

void SwiftBridgeEngineRegistry::SetScheduleFrameCallback(
    ScheduleFrameCallback callback) {
  schedule_frame_callback_ = std::move(callback);
}

const SwiftBridgeEngineRegistry::ScheduleFrameCallback&
SwiftBridgeEngineRegistry::GetScheduleFrameCallback() {
  return schedule_frame_callback_;
}

void SwiftBridgeEngineRegistry::SetDevicePixelRatio(float dpr) {
  device_pixel_ratio_ = dpr;
}

float SwiftBridgeEngineRegistry::GetDevicePixelRatio() {
  return device_pixel_ratio_;
}

bool SwiftBridgeEngineRegistry::FrameWasRendered() {
  return frame_rendered_;
}

void SwiftBridgeEngineRegistry::SetFrameRendered() {
  frame_rendered_ = true;
}

void SwiftBridgeEngineRegistry::ResetFrameRendered() {
  frame_rendered_ = false;
}

void SwiftBridgeEngineRegistry::SetFontCollection(void* font_collection_ptr) {
  // Delete old storage if present.
  if (font_collection_storage_) {
    delete static_cast<std::shared_ptr<txt::FontCollection>*>(
        font_collection_storage_);
    font_collection_storage_ = nullptr;
  }
  if (font_collection_ptr) {
    // Copy the shared_ptr so we hold a reference.
    font_collection_storage_ = new std::shared_ptr<txt::FontCollection>(
        *static_cast<std::shared_ptr<txt::FontCollection>*>(
            font_collection_ptr));
  }
}

void* SwiftBridgeEngineRegistry::GetFontCollection() {
  return font_collection_storage_;
}

void SwiftBridgeEngineRegistry::Reset() {
  render_callback_ = nullptr;
  schedule_frame_callback_ = nullptr;
  device_pixel_ratio_ = 1.0f;
  frame_rendered_ = false;
  if (font_collection_storage_) {
    delete static_cast<std::shared_ptr<txt::FontCollection>*>(
        font_collection_storage_);
    font_collection_storage_ = nullptr;
  }
}

}  // namespace flutter::swift_bridge
