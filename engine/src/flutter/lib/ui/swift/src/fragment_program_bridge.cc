// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/fragment_program_bridge.h"

#include <cstring>

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/effects/dl_color_source.h"
#include "flutter/display_list/effects/dl_image_filter.h"
#include "flutter/display_list/effects/dl_runtime_effect.h"
#include "flutter/display_list/effects/dl_runtime_effect_skia.h"
#include "flutter/fml/mapping.h"
#include "flutter/impeller/runtime_stage/runtime_stage.h"
#include "impeller/core/runtime_types.h"
#include "third_party/skia/include/core/SkString.h"
#include "third_party/skia/include/effects/SkRuntimeEffect.h"

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace flutter::swift_bridge {

namespace {
// Convert RuntimeStageBackend to human-readable string for error messages.
// Same as RuntimeStageBackendToString in fragment_program.cc:30-44.
std::string RuntimeStageBackendToString(
    impeller::RuntimeStageBackend backend) {
  switch (backend) {
    case impeller::RuntimeStageBackend::kSkSL:
      return "SkSL";
    case impeller::RuntimeStageBackend::kMetal:
      return "Metal";
    case impeller::RuntimeStageBackend::kOpenGLES:
      return "OpenGLES";
    case impeller::RuntimeStageBackend::kVulkan:
      return "Vulkan";
    case impeller::RuntimeStageBackend::kOpenGLES3:
      return "OpenGLES3";
  }
}

// Convert bridge Backend enum to impeller::RuntimeStageBackend
impeller::RuntimeStageBackend ToImpellerBackend(
    FragmentProgramBridge::Backend backend) {
  switch (backend) {
    case FragmentProgramBridge::Backend::kSkSL:
      return impeller::RuntimeStageBackend::kSkSL;
    case FragmentProgramBridge::Backend::kMetal:
      return impeller::RuntimeStageBackend::kMetal;
    case FragmentProgramBridge::Backend::kOpenGLES:
      return impeller::RuntimeStageBackend::kOpenGLES;
    case FragmentProgramBridge::Backend::kVulkan:
      return impeller::RuntimeStageBackend::kVulkan;
    case FragmentProgramBridge::Backend::kOpenGLES3:
      return impeller::RuntimeStageBackend::kOpenGLES3;
  }
}
}  // namespace

// Pimpl implementation holding actual Flutter types
struct FragmentProgramImpl {
  sk_sp<flutter::DlRuntimeEffect> runtime_effect;

  FragmentProgramImpl() : runtime_effect(nullptr) {}
};

FragmentProgramBridge::FragmentProgramBridge()
    : impl_(new FragmentProgramImpl()),
      uniform_float_count_(0),
      sampler_count_(0) {}

FragmentProgramBridge::~FragmentProgramBridge() {
  delete impl_;
}

const char* FragmentProgramBridge::InitFromData(
    const uint8_t* data,
    size_t data_size,
    Backend backend) {
  // Matches fragment_program.cc:46-147, but takes raw data instead of
  // loading from asset manager via UIDartState.
  //
  // DIFFERENCE FROM DART: The Dart implementation uses UIDartState::Current()
  // to access the AssetManager and load data. Here we receive data directly.
  // REASON: Avoids dependency on //flutter/lib/ui (Dart VM layer).

  if (!data || data_size == 0) {
    return strdup("No shader data provided");
  }

  // Create an fml::Mapping from the raw data (copies the data)
  auto mapping = std::make_unique<fml::NonOwnedMapping>(
      data, data_size, [](const uint8_t*, size_t) {});

  // We need to copy the data since NonOwnedMapping doesn't own it.
  // Use shared_ptr because fml::NonOwnedMapping's release callback uses
  // std::function which requires copyability.
  auto owned_data = std::make_shared<std::vector<uint8_t>>(data, data + data_size);
  auto owned_mapping = std::make_unique<fml::NonOwnedMapping>(
      owned_data->data(), owned_data->size(),
      [owned_data](const uint8_t*, size_t) {});

  auto runtime_stages =
      impeller::RuntimeStage::DecodeRuntimeStages(std::move(owned_mapping));

  if (runtime_stages.empty()) {
    return strdup("Data does not contain any shader data.");
  }

  impeller::RuntimeStageBackend impeller_backend = ToImpellerBackend(backend);
  std::shared_ptr<impeller::RuntimeStage> runtime_stage =
      runtime_stages[impeller_backend];
  if (!runtime_stage) {
    std::ostringstream stream;
    stream << "Data does not contain appropriate runtime stage data for "
              "current backend ("
           << RuntimeStageBackendToString(impeller_backend) << ")."
           << std::endl
           << "Found stages: ";
    for (const auto& kvp : runtime_stages) {
      if (kvp.second) {
        stream << RuntimeStageBackendToString(kvp.first) << " ";
      }
    }
    return strdup(stream.str().c_str());
  }

  // Count samplers and compute uniform size
  // Same as fragment_program.cc:85-94
  int sampled_image_count = 0;
  size_t other_uniforms_bytes = 0;
  for (const auto& uniform_description : runtime_stage->GetUniforms()) {
    if (uniform_description.type ==
        impeller::RuntimeUniformType::kSampledImage) {
      sampled_image_count++;
    } else {
      other_uniforms_bytes += uniform_description.GetSize();
    }
  }

  // Create the Skia runtime effect
  const auto& code_mapping = runtime_stage->GetCodeMapping();
  auto code_size = code_mapping->GetSize();
  const char* sksl =
      reinterpret_cast<const char*>(code_mapping->GetMapping());
  SkRuntimeEffect::Result result =
      SkRuntimeEffect::MakeForShader(SkString(sksl, code_size));
  if (result.effect == nullptr) {
    std::string msg = std::string("Invalid SkSL:\n") + sksl +
                      std::string("\nSkSL Error:\n") + result.errorText.c_str();
    return strdup(msg.c_str());
  }
  impl_->runtime_effect =
      flutter::DlRuntimeEffectSkia::Make(result.effect);

  // Store the counts
  // Same as fragment_program.cc:125-146
  sampler_count_ = sampled_image_count;

  size_t rounded_uniform_bytes =
      (other_uniforms_bytes + sizeof(float) - 1) & ~(sizeof(float) - 1);
  uniform_float_count_ =
      static_cast<int>(rounded_uniform_bytes / sizeof(float));

  return nullptr;  // Success
}

int FragmentProgramBridge::GetUniformFloatCount() const {
  return uniform_float_count_;
}

int FragmentProgramBridge::GetSamplerCount() const {
  return sampler_count_;
}

void* FragmentProgramBridge::MakeColorSource(
    const float* float_uniforms,
    int float_count,
    const void* const* children_ptrs,
    int children_count) const {
  if (!impl_->runtime_effect) {
    return nullptr;
  }

  // Build float uniforms buffer
  auto uniforms = std::make_shared<std::vector<uint8_t>>(
      float_count * sizeof(float));
  if (float_count > 0 && float_uniforms) {
    memcpy(uniforms->data(), float_uniforms, float_count * sizeof(float));
  }

  // Build children vector
  std::vector<std::shared_ptr<flutter::DlColorSource>> children;
  children.reserve(children_count);
  for (int i = 0; i < children_count; i++) {
    if (children_ptrs && children_ptrs[i]) {
      const auto* child_ptr =
          static_cast<const std::shared_ptr<flutter::DlColorSource>*>(
              children_ptrs[i]);
      children.push_back(*child_ptr);
    } else {
      children.push_back(nullptr);
    }
  }

  // Same as fragment_program.cc:149-154
  auto result = flutter::DlColorSource::MakeRuntimeEffect(
      impl_->runtime_effect, children, std::move(uniforms));

  // Return as opaque pointer - the shared_ptr is heap-allocated
  // so the caller can take ownership
  auto* heap_ptr =
      new std::shared_ptr<flutter::DlColorSource>(std::move(result));
  return static_cast<void*>(heap_ptr);
}

void* FragmentProgramBridge::MakeImageFilter(
    const float* float_uniforms,
    int float_count,
    const void* const* children_ptrs,
    int children_count) const {
  if (!impl_->runtime_effect) {
    return nullptr;
  }

  // Build float uniforms buffer
  auto uniforms = std::make_shared<std::vector<uint8_t>>(
      float_count * sizeof(float));
  if (float_count > 0 && float_uniforms) {
    memcpy(uniforms->data(), float_uniforms, float_count * sizeof(float));
  }

  // Build children vector
  std::vector<std::shared_ptr<flutter::DlColorSource>> children;
  children.reserve(children_count);
  for (int i = 0; i < children_count; i++) {
    if (children_ptrs && children_ptrs[i]) {
      const auto* child_ptr =
          static_cast<const std::shared_ptr<flutter::DlColorSource>*>(
              children_ptrs[i]);
      children.push_back(*child_ptr);
    } else {
      children.push_back(nullptr);
    }
  }

  // Same as fragment_program.cc:156-161
  auto result = flutter::DlImageFilter::MakeRuntimeEffect(
      impl_->runtime_effect, children, std::move(uniforms));

  // Return as opaque pointer
  auto* heap_ptr =
      new std::shared_ptr<flutter::DlImageFilter>(std::move(result));
  return static_cast<void*>(heap_ptr);
}

}  // namespace flutter::swift_bridge
