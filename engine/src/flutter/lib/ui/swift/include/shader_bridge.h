// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_SHADER_BRIDGE_H_
#define FLUTTER_SWIFT_SHADER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class ShaderBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainShaderBridge(flutter::swift_bridge::ShaderBridge* p) noexcept;
void ReleaseShaderBridge(flutter::swift_bridge::ShaderBridge* p) noexcept;

namespace flutter::swift_bridge {

/// C++ bridge wrapping Flutter's base Shader class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:4609-4647`
///
/// This is the base class for all shader types (Gradient, ImageShader, FragmentShader).
/// It provides dispose tracking functionality but does not wrap a specific shader
/// implementation. Subclasses (GradientBridge, ImageShaderBridge, etc.) will hold
/// the actual DlColorSource shader objects.
///
/// Architecture:
/// - Swift Shader class wraps ShaderBridge
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - Dispose() marks the shader as disposed (debug tracking only in Dart)
/// - Actual shader resources are held by subclasses
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainShaderBridge, ReleaseShaderBridge)
        ShaderBridge
    : public IntrusiveReferenceCounted<ShaderBridge> {
 public:
  /// Creates a ShaderBridge.
  ///
  /// Note: This is a base class. In practice, subclasses like GradientBridge
  /// will be instantiated instead.
  ShaderBridge();

  /// Whether dispose() has been called.
  ///
  /// **Dart Source:** `painting.dart:4619-4629`
  /// **Original:** `bool get debugDisposed`
  ///
  /// This is for debug tracking only.
  bool DebugDisposed() const;

  /// Release the resources used by this shader.
  ///
  /// **Dart Source:** `painting.dart:4640-4646`
  /// **Original:** `void dispose()`
  ///
  /// After calling this method, the shader is marked as disposed.
  /// Subclasses should override this to release their specific resources.
  virtual void Dispose();

  virtual ~ShaderBridge();

 private:
  ShaderBridge(const ShaderBridge&) = delete;
  ShaderBridge& operator=(const ShaderBridge&) = delete;

  bool debug_disposed_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainShaderBridge(flutter::swift_bridge::ShaderBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseShaderBridge(flutter::swift_bridge::ShaderBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_SHADER_BRIDGE_H_
