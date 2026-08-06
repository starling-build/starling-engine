// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_CHANNEL_BUFFERS_BRIDGE_H_
#define FLUTTER_SWIFT_CHANNEL_BUFFERS_BRIDGE_H_

#include "swift_bridge_export.h"

#include <swift/bridging>

namespace flutter::swift_bridge {

/// Callback type for sending channel update notifications.
///
/// The engine registers a callback of this type to receive channel update
/// notifications from Swift code.
///
/// @param name The name of the channel being updated.
/// @param listening Whether the channel is now being listened to (true) or
///                  the listener was cleared (false).
using SendChannelUpdateCallback = void (*)(const char* name, bool listening);

/// C++ bridge for ChannelBuffers native methods.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/channel_buffers.dart`
///
/// This bridge provides the C++ side of the channel buffers functionality
/// that Swift code can call. The engine must register a callback via
/// `SetSendChannelUpdateCallback` before calling `SendChannelUpdate`.
///
/// Architecture:
/// - Swift calls `ChannelBuffersBridge::SendChannelUpdate()`
/// - Bridge calls the registered callback
/// - Callback implementation (in engine) calls the appropriate RuntimeDelegate method
class FLUTTER_SWIFT_BRIDGE_EXPORT ChannelBuffersBridge {
 public:
  /// Register a callback to be called when Swift code sends a channel update.
  ///
  /// This should be called by the engine during initialization to register
  /// the callback that will forward channel updates to the RuntimeDelegate.
  ///
  /// @param callback The callback function to register, or nullptr to clear.
  static void SetSendChannelUpdateCallback(SendChannelUpdateCallback callback);

  /// Send a channel update notification to the engine.
  ///
  /// **Dart Source:** `channel_buffers.dart:401-402`
  /// **Original:** `@Native<Void Function(Handle, Bool)>(symbol: 'PlatformConfigurationNativeApi::SendChannelUpdate')`
  ///
  /// This method notifies the engine that a channel's listener has been set
  /// or cleared. The engine uses this information to optimize message routing.
  ///
  /// @param name The name of the channel (null-terminated UTF-8 string).
  /// @param listening Whether the channel is now being listened to.
  static void SendChannelUpdate(const char* name, bool listening);

 private:
  static SendChannelUpdateCallback send_channel_update_callback_;
};

}  // namespace flutter::swift_bridge

#endif  // FLUTTER_SWIFT_CHANNEL_BUFFERS_BRIDGE_H_
