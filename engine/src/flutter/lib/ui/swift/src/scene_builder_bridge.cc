// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/scene_builder_bridge.h"
#include "include/color_filter_bridge.h"
#include "include/engine_layer_bridge.h"
#include "include/image_filter_bridge.h"
#include "include/path_bridge.h"
#include "include/picture_bridge.h"
#include "include/scene_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_blend_mode.h"
#include "flutter/display_list/geometry/dl_geometry_types.h"
#include "flutter/display_list/geometry/dl_path.h"
#include "flutter/flow/layers/backdrop_filter_layer.h"
#include "flutter/flow/layers/clip_path_layer.h"
#include "flutter/flow/layers/clip_rect_layer.h"
#include "flutter/flow/layers/clip_rrect_layer.h"
#include "flutter/flow/layers/clip_rsuperellipse_layer.h"
#include "flutter/flow/layers/color_filter_layer.h"
#include "flutter/flow/layers/container_layer.h"
#include "flutter/flow/layers/display_list_layer.h"
#include "flutter/flow/layers/image_filter_layer.h"
#include "flutter/flow/layers/layer.h"
#include "flutter/flow/layers/opacity_layer.h"
#include "flutter/flow/layers/performance_overlay_layer.h"
#include "flutter/flow/layers/platform_view_layer.h"
#include "flutter/flow/layers/shader_mask_layer.h"
#include "flutter/flow/layers/texture_layer.h"
#include "flutter/flow/layers/transform_layer.h"
#include "flutter/impeller/geometry/round_superellipse.h"
#include "flutter/impeller/geometry/rounding_radii.h"
#include "third_party/skia/include/core/SkPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

namespace flutter::swift_bridge {

namespace {

// Inline SafeNarrow to avoid dependency on //flutter/lib/ui:ui
inline float SafeNarrow(double value) {
  if (std::isinf(value) || std::isnan(value)) {
    return static_cast<float>(value);
  }
  return std::clamp(static_cast<float>(value),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::max());
}

// Convert filter quality index to DlImageSampling.
// Matches ImageFilter::SamplingFromIndex from painting/image_filter.cc:36-45.
static const std::array<DlImageSampling, 4> kFilterQualities = {
    DlImageSampling::kNearestNeighbor,
    DlImageSampling::kLinear,
    DlImageSampling::kMipmapLinear,
    DlImageSampling::kCubic,
};

inline DlImageSampling SamplingFromIndex(int filter_quality_index) {
  if (filter_quality_index < 0) {
    return kFilterQualities.front();
  } else if (static_cast<size_t>(filter_quality_index) >=
             kFilterQualities.size()) {
    return kFilterQualities.back();
  } else {
    return kFilterQualities[filter_quality_index];
  }
}

// Helper to assign old layer if present.
template <typename T>
inline void AssignOldLayerIfPresent(
    std::shared_ptr<T>& layer,
    EngineLayerBridge* old_layer) {
  if (old_layer && !old_layer->IsDisposed()) {
    const void* old_ptr = old_layer->GetLayerPtr();
    if (old_ptr) {
      const auto& old_container =
          *static_cast<const std::shared_ptr<flutter::ContainerLayer>*>(
              old_ptr);
      if (old_container) {
        layer->AssignOldLayer(old_container.get());
      }
    }
  }
}

// Helper to create an EngineLayerBridge from a ContainerLayer shared_ptr.
template <typename T>
inline EngineLayerBridge* MakeEngineLayerBridge(
    std::shared_ptr<T>& layer) {
  auto as_container =
      std::static_pointer_cast<flutter::ContainerLayer>(layer);
  return new EngineLayerBridge(
      static_cast<const void*>(&as_container));
}

}  // namespace

// Pimpl implementation holding the actual Flutter SceneBuilder state.
// We replicate the SceneBuilder logic directly rather than wrapping
// the Dart-specific flutter::SceneBuilder class, to avoid Dart VM
// dependencies (Dart_Handle, RefCountedDartWrappable, etc.).
struct SceneBuilderImpl {
  std::vector<std::shared_ptr<flutter::ContainerLayer>> layer_stack;

  SceneBuilderImpl() {
    // Add a ContainerLayer as the root layer, matching
    // SceneBuilder::SceneBuilder() in scene_builder.cc:35-39
    layer_stack.push_back(std::make_shared<flutter::ContainerLayer>());
  }

  void AddLayer(std::shared_ptr<flutter::Layer> layer) {
    if (!layer_stack.empty()) {
      layer_stack.back()->Add(std::move(layer));
    }
  }

  void PushLayer(std::shared_ptr<flutter::ContainerLayer> layer) {
    AddLayer(layer);
    layer_stack.push_back(std::move(layer));
  }

  void PopLayer() {
    // Never pop the root layer, matching scene_builder.cc:315-320
    if (layer_stack.size() > 1) {
      layer_stack.pop_back();
    }
  }
};

SceneBuilderBridge::SceneBuilderBridge() {
  impl_ = new SceneBuilderImpl();
}

SceneBuilderBridge::~SceneBuilderBridge() {
  delete impl_;
}

EngineLayerBridge* SceneBuilderBridge::PushTransform(
    const double* matrix4,
    EngineLayerBridge* old_layer) {
  // Convert 16 doubles (column-major) to DlMatrix, matching
  // scene_builder.cc:46 (ToDlMatrix) and matrix.cc:46-56
  // clang-format off
  DlMatrix matrix = DlMatrix::MakeColumn(
      SafeNarrow(matrix4[ 0]), SafeNarrow(matrix4[ 1]),
      SafeNarrow(matrix4[ 2]), SafeNarrow(matrix4[ 3]),
      SafeNarrow(matrix4[ 4]), SafeNarrow(matrix4[ 5]),
      SafeNarrow(matrix4[ 6]), SafeNarrow(matrix4[ 7]),
      SafeNarrow(matrix4[ 8]), SafeNarrow(matrix4[ 9]),
      SafeNarrow(matrix4[10]), SafeNarrow(matrix4[11]),
      SafeNarrow(matrix4[12]), SafeNarrow(matrix4[13]),
      SafeNarrow(matrix4[14]), SafeNarrow(matrix4[15]));
  // clang-format on

  auto layer = std::make_shared<flutter::TransformLayer>(matrix);
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushOffset(
    double dx,
    double dy,
    EngineLayerBridge* old_layer) {
  // Matches scene_builder.cc:62
  DlMatrix matrix =
      DlMatrix::MakeTranslation({SafeNarrow(dx), SafeNarrow(dy)});
  auto layer = std::make_shared<flutter::TransformLayer>(matrix);
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushClipRect(
    double left,
    double top,
    double right,
    double bottom,
    int clip_behavior,
    EngineLayerBridge* old_layer) {
  // Matches scene_builder.cc:79-80
  DlRect clip_rect = DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                       SafeNarrow(right), SafeNarrow(bottom));
  auto layer = std::make_shared<flutter::ClipRectLayer>(
      clip_rect, static_cast<flutter::Clip>(clip_behavior));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushClipRRect(
    const float* rrect_values,
    int clip_behavior,
    EngineLayerBridge* old_layer) {
  // Parse 12 floats matching rrect.cc:30-43
  // [left, top, right, bottom, tlRadiusX, tlRadiusY, trRadiusX, trRadiusY,
  //  brRadiusX, brRadiusY, blRadiusX, blRadiusY]
  flutter::DlRect raw_rect = flutter::DlRect::MakeLTRB(
      rrect_values[0], rrect_values[1], rrect_values[2], rrect_values[3]);

  // Flutter has radii in TL,TR,BR,BL (clockwise) order,
  // but Impeller uses TL,TR,BL,BR (zig-zag) order
  impeller::RoundingRadii radii = {
      .top_left = flutter::DlSize(rrect_values[4], rrect_values[5]),
      .top_right = flutter::DlSize(rrect_values[6], rrect_values[7]),
      .bottom_left = flutter::DlSize(rrect_values[10], rrect_values[11]),
      .bottom_right = flutter::DlSize(rrect_values[8], rrect_values[9]),
  };

  auto rrect =
      flutter::DlRoundRect::MakeRectRadii(raw_rect.GetPositive(), radii);
  auto layer = std::make_shared<flutter::ClipRRectLayer>(
      rrect, static_cast<flutter::Clip>(clip_behavior));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushClipRSuperellipse(
    double left,
    double top,
    double right,
    double bottom,
    double tl_radius_x,
    double tl_radius_y,
    double tr_radius_x,
    double tr_radius_y,
    double br_radius_x,
    double br_radius_y,
    double bl_radius_x,
    double bl_radius_y,
    int clip_behavior,
    EngineLayerBridge* old_layer) {
  // Build bounds matching rsuperellipse.cc:15-22
  flutter::DlRect bounds =
      flutter::DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                 SafeNarrow(right), SafeNarrow(bottom))
          .GetPositive();

  // Build radii matching rsuperellipse.cc:24-40
  // Flutter has radii in TL,TR,BR,BL (clockwise) order,
  // but Impeller uses TL,TR,BL,BR (zig-zag) order
  impeller::RoundingRadii radii{
      .top_left = flutter::DlSize(SafeNarrow(tl_radius_x),
                                  SafeNarrow(tl_radius_y)),
      .top_right = flutter::DlSize(SafeNarrow(tr_radius_x),
                                   SafeNarrow(tr_radius_y)),
      .bottom_left = flutter::DlSize(SafeNarrow(bl_radius_x),
                                     SafeNarrow(bl_radius_y)),
      .bottom_right = flutter::DlSize(SafeNarrow(br_radius_x),
                                      SafeNarrow(br_radius_y)),
  };

  auto rse = flutter::DlRoundSuperellipse::MakeRectRadii(bounds, radii);
  auto layer = std::make_shared<flutter::ClipRSuperellipseLayer>(
      rse, static_cast<flutter::Clip>(clip_behavior));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushClipPath(
    PathBridge* path,
    int clip_behavior,
    EngineLayerBridge* old_layer) {
  // Get the SkPath from PathBridge and construct DlPath
  // Matches scene_builder.cc:127-128
  const void* sk_path_ptr = path->GetSkPathPtr();
  const SkPath& sk_path = *static_cast<const SkPath*>(sk_path_ptr);
  DlPath dl_path(sk_path);

  auto layer = std::make_shared<flutter::ClipPathLayer>(
      dl_path, static_cast<flutter::Clip>(clip_behavior));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushOpacity(
    int alpha,
    double dx,
    double dy,
    EngineLayerBridge* old_layer) {
  // Matches scene_builder.cc:142-143
  auto layer = std::make_shared<flutter::OpacityLayer>(
      alpha, DlPoint(SafeNarrow(dx), SafeNarrow(dy)));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushColorFilter(
    ColorFilterBridge* color_filter,
    EngineLayerBridge* old_layer) {
  // Get the DlColorFilter from ColorFilterBridge
  // Matches scene_builder.cc:155-156
  const void* filter_ptr = color_filter->GetFilterPtr();
  std::shared_ptr<const DlColorFilter> dl_filter;
  if (filter_ptr) {
    dl_filter = *static_cast<
        const std::shared_ptr<const DlColorFilter>*>(filter_ptr);
  }

  auto layer = std::make_shared<flutter::ColorFilterLayer>(dl_filter);
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushImageFilter(
    ImageFilterBridge* image_filter,
    double dx,
    double dy,
    EngineLayerBridge* old_layer) {
  // Get the DlImageFilter from ImageFilterBridge
  // Matches scene_builder.cc:170-172
  // ImageFilter uses DlTileMode::kDecal for pushImageFilter
  const void* filter_ptr =
      image_filter->GetFilterPtr(static_cast<int>(DlTileMode::kDecal));
  std::shared_ptr<DlImageFilter> dl_filter;
  if (filter_ptr) {
    dl_filter =
        *static_cast<const std::shared_ptr<DlImageFilter>*>(filter_ptr);
  }

  auto layer = std::make_shared<flutter::ImageFilterLayer>(
      dl_filter, DlPoint(SafeNarrow(dx), SafeNarrow(dy)));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushBackdropFilter(
    ImageFilterBridge* filter,
    int blend_mode,
    int64_t backdrop_id,
    bool has_backdrop_id,
    EngineLayerBridge* old_layer) {
  // Get the DlImageFilter from ImageFilterBridge
  // Matches scene_builder.cc:194-196
  // BackdropFilter uses DlTileMode::kMirror
  const void* filter_ptr =
      filter->GetFilterPtr(static_cast<int>(DlTileMode::kMirror));
  std::shared_ptr<DlImageFilter> dl_filter;
  if (filter_ptr) {
    dl_filter =
        *static_cast<const std::shared_ptr<DlImageFilter>*>(filter_ptr);
  }

  std::optional<int64_t> converted_backdrop_id;
  if (has_backdrop_id) {
    converted_backdrop_id = backdrop_id;
  }

  auto layer = std::make_shared<flutter::BackdropFilterLayer>(
      dl_filter, static_cast<DlBlendMode>(blend_mode),
      converted_backdrop_id);
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

EngineLayerBridge* SceneBuilderBridge::PushShaderMask(
    const void* shader_ptr,
    double mask_rect_left,
    double mask_rect_top,
    double mask_rect_right,
    double mask_rect_bottom,
    int blend_mode,
    int filter_quality_index,
    EngineLayerBridge* old_layer) {
  // Matches scene_builder.cc:214-219
  DlRect rect =
      DlRect::MakeLTRB(SafeNarrow(mask_rect_left), SafeNarrow(mask_rect_top),
                        SafeNarrow(mask_rect_right), SafeNarrow(mask_rect_bottom));

  // Extract the DlColorSource from the opaque shader pointer.
  // The Dart SceneBuilder calls shader->shader(sampling) to get a
  // sampling-aware shader variant. Our bridge receives a pre-resolved
  // DlColorSource from GetShaderPtr(), so we don't need to apply sampling
  // here. The filter_quality_index parameter is preserved in the API for
  // future use if needed.
  //
  // DIFFERENCE FROM DART: We pass the shader directly without calling
  // shader->shader(sampling) since our bridge receives pre-resolved
  // DlColorSource objects.
  // REASON: The bridge layer receives pre-resolved DlColorSource objects.
  (void)filter_quality_index;  // Reserved for future use

  std::shared_ptr<DlColorSource> shader;
  if (shader_ptr) {
    shader =
        *static_cast<const std::shared_ptr<DlColorSource>*>(shader_ptr);
  }

  auto layer = std::make_shared<flutter::ShaderMaskLayer>(
      shader, rect, static_cast<DlBlendMode>(blend_mode));
  impl_->PushLayer(layer);
  AssignOldLayerIfPresent(layer, old_layer);
  return MakeEngineLayerBridge(layer);
}

void SceneBuilderBridge::Pop() {
  // Matches scene_builder.cc:232-234
  impl_->PopLayer();
}

void SceneBuilderBridge::AddRetained(EngineLayerBridge* retained_layer) {
  // Matches scene_builder.cc:228-230
  if (!retained_layer || retained_layer->IsDisposed()) {
    return;
  }
  const void* layer_ptr = retained_layer->GetLayerPtr();
  if (layer_ptr) {
    auto layer =
        *static_cast<const std::shared_ptr<flutter::ContainerLayer>*>(
            layer_ptr);
    impl_->AddLayer(layer);
  }
}

void SceneBuilderBridge::AddPerformanceOverlay(uint64_t enabled_options,
                                                double left,
                                                double top,
                                                double right,
                                                double bottom) {
  // Matches scene_builder.cc:281-292
  DlRect rect = DlRect::MakeLTRB(SafeNarrow(left), SafeNarrow(top),
                                  SafeNarrow(right), SafeNarrow(bottom));
  auto layer =
      std::make_unique<flutter::PerformanceOverlayLayer>(enabled_options);
  layer->set_paint_bounds(rect);
  impl_->AddLayer(std::move(layer));
}

void SceneBuilderBridge::AddPicture(double dx,
                                     double dy,
                                     PictureBridge* picture,
                                     int hints) {
  // Matches scene_builder.cc:236-253
  if (!picture) {
    return;
  }

  const void* dl_ptr = picture->GetDisplayListPtr();
  if (dl_ptr) {
    auto display_list =
        *static_cast<const sk_sp<flutter::DisplayList>*>(dl_ptr);
    auto layer = std::make_unique<flutter::DisplayListLayer>(
        DlPoint(SafeNarrow(dx), SafeNarrow(dy)), display_list, !!(hints & 1),
        !!(hints & 2));
    impl_->AddLayer(std::move(layer));
  }
}

void SceneBuilderBridge::AddTexture(double dx,
                                     double dy,
                                     double width,
                                     double height,
                                     int64_t texture_id,
                                     bool freeze,
                                     int filter_quality_index) {
  // Matches scene_builder.cc:255-268
  auto sampling = SamplingFromIndex(filter_quality_index);
  auto layer = std::make_unique<flutter::TextureLayer>(
      DlPoint(SafeNarrow(dx), SafeNarrow(dy)),
      DlSize(SafeNarrow(width), SafeNarrow(height)), texture_id, freeze,
      sampling);
  impl_->AddLayer(std::move(layer));
}

void SceneBuilderBridge::AddPlatformView(double dx,
                                          double dy,
                                          double width,
                                          double height,
                                          int64_t view_id) {
  // Matches scene_builder.cc:270-278
  auto layer = std::make_unique<flutter::PlatformViewLayer>(
      DlPoint(SafeNarrow(dx), SafeNarrow(dy)),
      DlSize(SafeNarrow(width), SafeNarrow(height)), view_id);
  impl_->AddLayer(std::move(layer));
}

SceneBridge* SceneBuilderBridge::Build() {
  // Matches scene_builder.cc:294-300
  // Extract the root layer and create a SceneBridge.
  if (impl_->layer_stack.empty()) {
    return new SceneBridge(nullptr);
  }

  auto root_layer = std::move(impl_->layer_stack[0]);
  impl_->layer_stack.clear();

  // SceneBridge takes a pointer to shared_ptr<Layer>
  auto as_layer = std::shared_ptr<flutter::Layer>(std::move(root_layer));
  return new SceneBridge(static_cast<const void*>(&as_layer));
}

}  // namespace flutter::swift_bridge
