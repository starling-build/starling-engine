// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_FRAGMENT_SHADER_BRIDGE_H_
#define FLUTTER_SWIFT_FRAGMENT_SHADER_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"
#include "fragment_program_bridge.h"
#include "image_bridge.h"

#include <cstddef>
#include <cstdint>

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class FragmentShaderBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainFragmentShaderBridge(
    flutter::swift_bridge::FragmentShaderBridge* p) noexcept;
void ReleaseFragmentShaderBridge(
    flutter::swift_bridge::FragmentShaderBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct FragmentShaderImpl;

/// C++ bridge wrapping Flutter's ReusableFragmentShader class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:5270-5388`
/// **Original:** `FragmentShader` class (extends Shader)
///
/// FragmentShader holds uniform float data and image samplers for a
/// fragment program shader. It wraps the underlying SkData for uniforms
/// and DlColorSource array for samplers.
///
/// Architecture:
/// - Swift FragmentShader class wraps FragmentShaderBridge
/// - FragmentShaderBridge holds uniform data (SkData) and samplers
/// - FragmentProgramBridge creates DlColorSource/DlImageFilter from them
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
///
/// The Dart implementation uses a Float32List backed by external typed data
/// pointing into the SkData buffer. The Swift bridge exposes SetFloat/GetFloat
/// methods instead, since Swift cannot create an external typed data view
/// into C++ memory in the same way.
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainFragmentShaderBridge,
                           ReleaseFragmentShaderBridge)
        FragmentShaderBridge
    : public IntrusiveReferenceCounted<FragmentShaderBridge> {
 public:
  /// Creates a FragmentShaderBridge for the given program with the specified
  /// uniform counts.
  ///
  /// **Dart Source:** `painting.dart:5284-5288`
  /// **Original:** `FragmentShader._(FragmentProgram program, {String? debugName})`
  ///
  /// The constructor allocates uniform data buffer sized to hold:
  ///   (float_uniforms + 2 * sampler_uniforms) floats
  /// The extra 2 floats per sampler store the image width and height.
  ///
  /// @param program The FragmentProgramBridge that owns the shader program
  /// @param float_uniforms Number of float uniforms expected
  /// @param sampler_uniforms Number of sampler (image) uniforms expected
  FragmentShaderBridge(FragmentProgramBridge* program,
                       int float_uniforms,
                       int sampler_uniforms);

  /// Sets the float uniform at the given index.
  ///
  /// **Dart Source:** `painting.dart:5338-5341`
  /// **Original:** `void setFloat(int index, double value)`
  ///
  /// @param index The index of the float uniform to set
  /// @param value The value to set
  void SetFloat(int index, float value);

  /// Gets the float uniform at the given index.
  ///
  /// @param index The index of the float uniform to get
  /// @return The float value at the given index
  float GetFloat(int index) const;

  /// Sets the image sampler at the given index.
  ///
  /// **Dart Source:** `painting.dart:5350-5354`
  /// **Original:** `void setImageSampler(int index, Image image)`
  ///
  /// Creates a DlColorSource from the image with clamped tile modes
  /// and stores it in the samplers array. Also stores the image
  /// dimensions in the uniform data for the shader to access.
  ///
  /// @param index The index of the sampler uniform to set
  /// @param image The ImageBridge containing the image to sample
  /// @return true on success, false if index is out of bounds or image is invalid
  bool SetImageSampler(int index, const ImageBridge* image);

  /// Validates that all sampler uniforms have been set.
  ///
  /// **Dart Source:** `painting.dart:5380-5381`
  /// **Original:** `bool _validateSamplers()`
  ///
  /// @return true if all samplers are non-null, false otherwise
  bool ValidateSamplers() const;

  /// Validates this shader can be used as an image filter.
  ///
  /// **Dart Source:** `painting.dart:5383-5384`
  /// **Original:** `bool _validateImageFilter()`
  ///
  /// Image filters require at least one sampler. The first sampler
  /// does not need to be set (it binds the input texture), but all
  /// remaining samplers must be set.
  ///
  /// @return true if valid for use as image filter, false otherwise
  bool ValidateImageFilter() const;

  /// Returns the number of float uniforms.
  int GetFloatCount() const;

  /// Returns the number of sampler uniforms.
  int GetSamplerCount() const;

  /// Returns a pointer to the internal DlColorSource shared_ptr.
  ///
  /// Creates and caches the color source on first call (or when uniforms change).
  /// Returns nullptr if the shader is disposed or has no program.
  ///
  /// @return Pointer to std::shared_ptr<DlColorSource>, or nullptr
  SWIFT_RETURNS_INDEPENDENT_VALUE
  const void* GetShaderPtr() const;

  /// Creates a DlColorSource from the current uniform data and samplers.
  ///
  /// **Dart Source:** `painting/fragment_shader.cc:108-124`
  /// **Original:** `shader()` override
  ///
  /// Takes a copy of uniform data for thread-safe consumption by the
  /// render thread.
  ///
  /// @return Opaque pointer to the created DlColorSource (shared ownership)
  void* MakeColorSource() const;

  /// Creates a DlImageFilter from the current uniform data and samplers.
  ///
  /// **Dart Source:** `painting/fragment_shader.cc:95-106`
  /// **Original:** `as_image_filter()` const
  ///
  /// Takes a copy of uniform data for thread-safe consumption by the
  /// render thread.
  ///
  /// @return Opaque pointer to the created DlImageFilter (shared ownership)
  void* MakeImageFilter() const;

  /// Releases resources held by this shader.
  ///
  /// **Dart Source:** `painting.dart:5362-5366`
  /// **Original:** `void dispose()`
  ///
  /// Resets uniform data, clears program reference and samplers.
  void Dispose();

  /// Whether dispose has been called.
  bool IsDisposed() const;

  ~FragmentShaderBridge();

 private:
  FragmentShaderBridge(const FragmentShaderBridge&) = delete;
  FragmentShaderBridge& operator=(const FragmentShaderBridge&) = delete;

  // Pimpl - holds the actual Flutter types (SkData, DlColorSource)
  FragmentShaderImpl* impl_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainFragmentShaderBridge(
    flutter::swift_bridge::FragmentShaderBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseFragmentShaderBridge(
    flutter::swift_bridge::FragmentShaderBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_FRAGMENT_SHADER_BRIDGE_H_
