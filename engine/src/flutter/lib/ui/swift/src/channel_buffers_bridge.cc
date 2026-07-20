// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/channel_buffers_bridge.h"

namespace flutter::swift_bridge {

// Static member initialization
SendChannelUpdateCallback ChannelBuffersBridge::send_channel_update_callback_ =
    nullptr;

void ChannelBuffersBridge::SetSendChannelUpdateCallback(
    SendChannelUpdateCallback callback) {
  send_channel_update_callback_ = callback;
}

void ChannelBuffersBridge::SendChannelUpdate(const char* name, bool listening) {
  if (send_channel_update_callback_ != nullptr) {
    send_channel_update_callback_(name, listening);
  }
  // If no callback is registered, silently ignore the update.
  // This can happen during engine initialization or in test scenarios.
}

}  // namespace flutter::swift_bridge
