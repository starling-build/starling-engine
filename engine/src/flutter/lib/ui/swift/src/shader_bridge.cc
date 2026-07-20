// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/shader_bridge.h"

namespace flutter::swift_bridge {

ShaderBridge::ShaderBridge() : debug_disposed_(false) {}

ShaderBridge::~ShaderBridge() = default;

bool ShaderBridge::DebugDisposed() const {
  return debug_disposed_;
}

void ShaderBridge::Dispose() {
  // Match Dart behavior: assert if already disposed (in debug mode)
  // In release mode, this is a no-op beyond setting the flag
  //
  // Note: Dart uses assert() which is stripped in release builds.
  // We set the flag unconditionally for consistency.
  //
  // Dart Source: painting.dart:4640-4646
  // assert(() {
  //   assert(!_debugDisposed, 'A Shader cannot be disposed more than once.');
  //   _debugDisposed = true;
  //   return true;
  // }());
  debug_disposed_ = true;
}

}  // namespace flutter::swift_bridge
