// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/fragment_shader_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_tile_mode.h"
#include "flutter/display_list/effects/dl_color_source.h"
#include "flutter/display_list/effects/dl_image_filter.h"
#include "flutter/display_list/effects/dl_runtime_effect.h"

#include "include/image_bridge.h"

#include <cstring>
#include <memory>
#include <vector>

// Forward declare the DlImage type used by ImageBridgeImpl
#include "flutter/display_list/image/dl_image.h"

namespace flutter::swift_bridge {

// Pimpl implementation holding actual Flutter types.
//
// Mirrors the member variables of flutter::ReusableFragmentShader:
// - uniform_data_: SkData buffer for float uniforms + image dimensions
// - samplers_: vector of DlColorSource for image samplers
// - float_count_: number of float uniforms
// - program_: reference to the FragmentProgramBridge that owns the shader program
struct FragmentShaderImpl {
  // The raw uniform data buffer. Sized to hold:
  //   (float_count + 2 * sampler_count) * sizeof(float)
  // The first float_count entries are user-set float uniforms.
  // After that, each sampler contributes 2 floats (width, height).
  //
  // Same as fragment_shader.h:56 - `sk_sp<SkData> uniform_data_`
  // We use a vector<uint8_t> instead of SkData to avoid SkData dependency
  // in the bridge layer. The data layout is identical.
  std::vector<uint8_t> uniform_data;

  // Image sampler color sources.
  // Same as fragment_shader.h:57 - `vector<shared_ptr<DlColorSource>> samplers_`
  std::vector<std::shared_ptr<flutter::DlColorSource>> samplers;

  // Number of float uniforms (not including image dimension floats).
  // Same as fragment_shader.h:58 - `size_t float_count_`
  size_t float_count;

  // Reference to the program bridge for creating color sources/image filters.
  FragmentProgramBridge* program;

  bool disposed;

  // Cached color source for GetShaderPtr()
  mutable std::shared_ptr<flutter::DlColorSource> cached_shader;

  FragmentShaderImpl(FragmentProgramBridge* prog,
                     size_t float_cnt,
                     size_t sampler_cnt)
      : uniform_data((float_cnt + 2 * sampler_cnt) * sizeof(float), 0),
        samplers(sampler_cnt),
        float_count(float_cnt),
        program(prog),
        disposed(false) {}
};

FragmentShaderBridge::FragmentShaderBridge(
    FragmentProgramBridge* program,
    int float_uniforms,
    int sampler_uniforms)
    : impl_(new FragmentShaderImpl(
          program,
          static_cast<size_t>(float_uniforms),
          static_cast<size_t>(sampler_uniforms))) {
  // Retain the program bridge to keep it alive while this shader exists.
  if (program) {
    program->Retain();
  }
}

FragmentShaderBridge::~FragmentShaderBridge() {
  if (impl_) {
    // Release the program bridge reference
    if (impl_->program) {
      impl_->program->Release();
    }
    delete impl_;
  }
}

void FragmentShaderBridge::SetFloat(int index, float value) {
  if (!impl_ || impl_->disposed) {
    return;
  }
  auto* floats = reinterpret_cast<float*>(impl_->uniform_data.data());
  floats[index] = value;
}

float FragmentShaderBridge::GetFloat(int index) const {
  if (!impl_ || impl_->disposed) {
    return 0.0f;
  }
  const auto* floats =
      reinterpret_cast<const float*>(impl_->uniform_data.data());
  return floats[index];
}

bool FragmentShaderBridge::SetImageSampler(int index,
                                           const ImageBridge* image) {
  if (!impl_ || impl_->disposed) {
    return false;
  }

  auto sampler_index = static_cast<size_t>(index);
  if (sampler_index >= impl_->samplers.size()) {
    return false;
  }

  if (!image || image->IsDisposed()) {
    return false;
  }

  // Get the underlying DlImage from the ImageBridge
  const void* dl_image_ptr = image->GetDlImagePtr();
  if (!dl_image_ptr) {
    return false;
  }

  // The DlImage pointer is a pointer to sk_sp<const DlImage>
  const auto& dl_image =
      *static_cast<const sk_sp<const flutter::DlImage>*>(dl_image_ptr);

  // Same as fragment_shader.cc:82-84
  // Create a DlColorSource from the image with clamped tile modes
  impl_->samplers[sampler_index] = flutter::DlColorSource::MakeImage(
      dl_image, flutter::DlTileMode::kClamp, flutter::DlTileMode::kClamp,
      flutter::DlImageSampling::kNearestNeighbor, nullptr);

  // Store image dimensions in uniform data
  // Same as fragment_shader.cc:89-92
  auto* uniform_floats =
      reinterpret_cast<float*>(impl_->uniform_data.data());
  uniform_floats[impl_->float_count + 2 * sampler_index] =
      static_cast<float>(image->Width());
  uniform_floats[impl_->float_count + 2 * sampler_index + 1] =
      static_cast<float>(image->Height());

  return true;
}

bool FragmentShaderBridge::ValidateSamplers() const {
  if (!impl_ || impl_->disposed) {
    return false;
  }
  // Same as fragment_shader.cc:50-60
  for (size_t i = 0; i < impl_->samplers.size(); i++) {
    if (impl_->samplers[i] == nullptr) {
      return false;
    }
  }
  return true;
}

bool FragmentShaderBridge::ValidateImageFilter() const {
  if (!impl_ || impl_->disposed) {
    return false;
  }
  // Same as fragment_shader.cc:128-142
  // Image filters require at least one sampler
  if (impl_->samplers.size() < 1) {
    return false;
  }
  // The first sampler does not need to be set (it binds the input texture).
  for (size_t i = 1; i < impl_->samplers.size(); i++) {
    if (impl_->samplers[i] == nullptr) {
      return false;
    }
  }
  return true;
}

int FragmentShaderBridge::GetFloatCount() const {
  if (!impl_) {
    return 0;
  }
  return static_cast<int>(impl_->float_count);
}

int FragmentShaderBridge::GetSamplerCount() const {
  if (!impl_) {
    return 0;
  }
  return static_cast<int>(impl_->samplers.size());
}

const void* FragmentShaderBridge::GetShaderPtr() const {
  if (!impl_ || impl_->disposed || !impl_->program) {
    return nullptr;
  }

  // Create/update the cached shader via MakeColorSource
  void* raw = MakeColorSource();
  if (!raw) {
    return nullptr;
  }
  // MakeColorSource returns a heap-allocated shared_ptr - take ownership
  auto* heap_ptr =
      static_cast<std::shared_ptr<flutter::DlColorSource>*>(raw);
  impl_->cached_shader = std::move(*heap_ptr);
  delete heap_ptr;
  return static_cast<const void*>(&impl_->cached_shader);
}

void* FragmentShaderBridge::MakeColorSource() const {
  if (!impl_ || impl_->disposed || !impl_->program) {
    return nullptr;
  }

  // Same as fragment_shader.cc:108-124
  // Take a copy of uniform data for thread-safe consumption on the render thread.
  size_t total_float_count =
      impl_->float_count + 2 * impl_->samplers.size();

  // Build children array of opaque pointers
  std::vector<const void*> children_ptrs;
  children_ptrs.reserve(impl_->samplers.size());
  for (const auto& sampler : impl_->samplers) {
    children_ptrs.push_back(
        sampler ? static_cast<const void*>(&sampler) : nullptr);
  }

  return impl_->program->MakeColorSource(
      reinterpret_cast<const float*>(impl_->uniform_data.data()),
      static_cast<int>(total_float_count),
      children_ptrs.data(),
      static_cast<int>(children_ptrs.size()));
}

void* FragmentShaderBridge::MakeImageFilter() const {
  if (!impl_ || impl_->disposed || !impl_->program) {
    return nullptr;
  }

  // Same as fragment_shader.cc:95-106
  size_t total_float_count =
      impl_->float_count + 2 * impl_->samplers.size();

  // Build children array of opaque pointers
  std::vector<const void*> children_ptrs;
  children_ptrs.reserve(impl_->samplers.size());
  for (const auto& sampler : impl_->samplers) {
    children_ptrs.push_back(
        sampler ? static_cast<const void*>(&sampler) : nullptr);
  }

  return impl_->program->MakeImageFilter(
      reinterpret_cast<const float*>(impl_->uniform_data.data()),
      static_cast<int>(total_float_count),
      children_ptrs.data(),
      static_cast<int>(children_ptrs.size()));
}

void FragmentShaderBridge::Dispose() {
  if (!impl_ || impl_->disposed) {
    return;
  }
  // Same as fragment_shader.cc:144-149
  impl_->uniform_data.clear();
  if (impl_->program) {
    impl_->program->Release();
    impl_->program = nullptr;
  }
  impl_->samplers.clear();
  impl_->disposed = true;
}

bool FragmentShaderBridge::IsDisposed() const {
  return !impl_ || impl_->disposed;
}

}  // namespace flutter::swift_bridge
