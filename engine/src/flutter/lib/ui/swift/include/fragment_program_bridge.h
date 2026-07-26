// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SWIFT_FRAGMENT_PROGRAM_BRIDGE_H_
#define FLUTTER_SWIFT_FRAGMENT_PROGRAM_BRIDGE_H_

#include <swift/bridging>
#include "intrusive_reference_counted.h"

#include <cstddef>
#include <cstdint>

namespace flutter::swift_bridge {

// Forward declaration for retain/release functions
class FragmentProgramBridge;

}  // namespace flutter::swift_bridge

// Free functions for SWIFT_SHARED_REFERENCE - must be at global scope
void RetainFragmentProgramBridge(
    flutter::swift_bridge::FragmentProgramBridge* p) noexcept;
void ReleaseFragmentProgramBridge(
    flutter::swift_bridge::FragmentProgramBridge* p) noexcept;

namespace flutter::swift_bridge {

// Forward declarations for pimpl
struct FragmentProgramImpl;

/// C++ bridge wrapping Flutter's FragmentProgram class.
///
/// **Dart Source:** `engine/src/flutter/lib/ui/painting.dart:5187-5268`
///
/// FragmentProgram loads compiled shader programs from assets and creates
/// FragmentShader instances. It wraps DlRuntimeEffect which provides the
/// actual shader functionality.
///
/// Architecture:
/// - Swift FragmentProgram class wraps FragmentProgramBridge
/// - FragmentProgramBridge holds DlRuntimeEffect via pimpl
/// - Swift ARC handles lifetime via SWIFT_SHARED_REFERENCE
/// - InitFromData loads the shader program from raw asset data
///
/// DIFFERENCE FROM DART: In Dart, the native _initFromAsset method accesses
/// UIDartState to load assets and determine the rendering backend. In the Swift
/// bridge, we take the raw asset data and backend type as parameters to avoid
/// depending on //flutter/lib/ui (the Dart UI layer being replaced).
/// REASON: The bridge must not depend on Dart VM infrastructure.
class __attribute__((visibility("default")))
    SWIFT_SHARED_REFERENCE(RetainFragmentProgramBridge,
                           ReleaseFragmentProgramBridge)
        FragmentProgramBridge
    : public IntrusiveReferenceCounted<FragmentProgramBridge> {
 public:
  /// Runtime stage backend enum values.
  enum class Backend : int {
    kSkSL = 0,
    kMetal = 1,
    kOpenGLES = 2,
    kVulkan = 3,
    kOpenGLES3 = 4,
  };

  /// Creates a FragmentProgramBridge.
  FragmentProgramBridge();

  /// Initialize the fragment program from raw asset data.
  ///
  /// **Dart Source:** `painting.dart:5194-5203`
  /// **Original:** `FragmentProgram._fromAsset(String assetKey)`
  ///
  /// DIFFERENCE FROM DART: Takes raw data and backend type instead of asset key.
  /// REASON: Avoids dependency on UIDartState (Dart VM infrastructure).
  /// The Swift layer is responsible for loading the asset data.
  ///
  /// Loads a compiled shader program from the given data.
  /// Returns an empty string on success, or an error message on failure.
  ///
  /// This method also computes the uniform float count and sampler count
  /// from the loaded shader's uniform descriptions.
  ///
  /// @param data Pointer to the raw asset data (compiled shader program)
  /// @param data_size Size of the data in bytes
  /// @param backend The runtime stage backend to use
  /// @return nullptr on success, or a strdup'd error message on failure
  ///         (caller must free the returned pointer)
  const char* InitFromData(const uint8_t* data,
                           size_t data_size,
                           Backend backend)
      SWIFT_RETURNS_INDEPENDENT_VALUE;

  /// Returns the number of float uniforms expected by the shader.
  ///
  /// **Dart Source:** `painting.dart:5254-5255`
  /// **Original:** `late int _uniformFloatCount;`
  ///
  /// This value is computed during InitFromData by examining the
  /// shader's uniform descriptions and calculating the total byte size
  /// of non-sampler uniforms, rounded up to float alignment.
  int GetUniformFloatCount() const;

  /// Returns the number of sampler uniforms (image samplers) expected.
  ///
  /// **Dart Source:** `painting.dart:5257-5258`
  /// **Original:** `late int _samplerCount;`
  ///
  /// This value is computed during InitFromData by counting uniforms
  /// of type SampledImage.
  int GetSamplerCount() const;

  /// Creates a DlColorSource from this program with the given uniforms and children.
  ///
  /// **Dart Source:** `painting/fragment_program.cc:149-154`
  /// **Original:** `FragmentProgram::MakeDlColorSource`
  ///
  /// @param float_uniforms Pointer to float uniform data
  /// @param float_count Number of floats in the uniform data
  /// @param children_ptrs Array of pointers to child DlColorSource objects (opaque)
  /// @param children_count Number of children
  /// @return Opaque pointer to the created DlColorSource (caller takes shared ownership)
  void* MakeColorSource(const float* float_uniforms,
                        int float_count,
                        const void* const* children_ptrs,
                        int children_count) const;

  /// Creates a DlImageFilter from this program with the given uniforms and children.
  ///
  /// **Dart Source:** `painting/fragment_program.cc:156-161`
  /// **Original:** `FragmentProgram::MakeDlImageFilter`
  ///
  /// @param float_uniforms Pointer to float uniform data
  /// @param float_count Number of floats in the uniform data
  /// @param children_ptrs Array of pointers to child DlColorSource objects (opaque)
  /// @param children_count Number of children
  /// @return Opaque pointer to the created DlImageFilter (caller takes shared ownership)
  void* MakeImageFilter(const float* float_uniforms,
                        int float_count,
                        const void* const* children_ptrs,
                        int children_count) const;

  ~FragmentProgramBridge();

 private:
  FragmentProgramBridge(const FragmentProgramBridge&) = delete;
  FragmentProgramBridge& operator=(const FragmentProgramBridge&) = delete;

  // Pimpl - holds the actual Flutter types (DlRuntimeEffect)
  FragmentProgramImpl* impl_;
  int uniform_float_count_;
  int sampler_count_;
};

}  // namespace flutter::swift_bridge

// Inline definitions for global retain/release functions
inline void RetainFragmentProgramBridge(
    flutter::swift_bridge::FragmentProgramBridge* p) noexcept {
  p->Retain();
}
inline void ReleaseFragmentProgramBridge(
    flutter::swift_bridge::FragmentProgramBridge* p) noexcept {
  p->Release();
}

#endif  // FLUTTER_SWIFT_FRAGMENT_PROGRAM_BRIDGE_H_
