// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "flutter/shell/platform/darwin/ios/ios_surface_metal_impeller.h"

#include <map>
#include <mutex>

#include "flutter/shell/common/raster_view_id.h"

#include "flutter/impeller/renderer/backend/metal/formats_mtl.h"
#include "flutter/impeller/renderer/context.h"
#include "flutter/shell/gpu/gpu_surface_metal_impeller.h"
#include "impeller/display_list/aiks_context.h"
#include "impeller/typographer/backends/skia/typographer_context_skia.h"
#include "impeller/typographer/typographer_context.h"

FLUTTER_ASSERT_ARC

namespace flutter {

namespace {
std::mutex& ViewLayersMutex() {
  static std::mutex m;
  return m;
}
std::map<int64_t, CAMetalLayer*>& ViewLayers() {
  static std::map<int64_t, CAMetalLayer*> layers;
  return layers;
}
}  // namespace

void IOSSurfaceMetalImpeller::RegisterLayerForView(int64_t view_id, CAMetalLayer* layer) {
  std::lock_guard<std::mutex> guard(ViewLayersMutex());
  ViewLayers()[view_id] = layer;
}

void IOSSurfaceMetalImpeller::UnregisterLayerForView(int64_t view_id) {
  std::lock_guard<std::mutex> guard(ViewLayersMutex());
  ViewLayers().erase(view_id);
}

IOSSurfaceMetalImpeller::IOSSurfaceMetalImpeller(CAMetalLayer* layer,
                                                 const std::shared_ptr<IOSContext>& context)
    : IOSSurface(context),
      GPUSurfaceMetalDelegate(MTLRenderTargetType::kCAMetalLayer),
      layer_(layer),
      impeller_context_(context ? context->GetImpellerContext() : nullptr),
      aiks_context_(context ? context->GetAiksContext() : nullptr) {
  if (!impeller_context_ || !aiks_context_) {
    return;
  }
  is_valid_ = true;
}

// |IOSSurface|
IOSSurfaceMetalImpeller::~IOSSurfaceMetalImpeller() = default;

// |IOSSurface|
bool IOSSurfaceMetalImpeller::IsValid() const {
  return is_valid_;
}

// |IOSSurface|
void IOSSurfaceMetalImpeller::UpdateStorageSizeIfNecessary() {
  // Nothing to do.
}

// |IOSSurface|
std::unique_ptr<Surface> IOSSurfaceMetalImpeller::CreateGPUSurface() {
  impeller_context_->UpdateOffscreenLayerPixelFormat(
      impeller::FromMTLPixelFormat(layer_.pixelFormat));
  return std::make_unique<GPUSurfaceMetalImpeller>(this,          //
                                                   aiks_context_  //
  );
}

// |GPUSurfaceMetalDelegate|
GPUCAMetalLayerHandle IOSSurfaceMetalImpeller::GetCAMetalLayer(const DlISize& frame_info) const {
  CAMetalLayer* layer = layer_;
  if (CurrentRasterViewId() != 0) {  // STARLING: a view of its own, not the implicit one
    std::lock_guard<std::mutex> guard(ViewLayersMutex());
    auto it = ViewLayers().find(CurrentRasterViewId());
    if (it == ViewLayers().end()) {
      return nullptr;  // an unregistered view draws nowhere, rather than over the first one
    }
    layer = it->second;
  }
  const auto drawable_size = CGSizeMake(frame_info.width, frame_info.height);
  if (!CGSizeEqualToSize(drawable_size, layer.drawableSize)) {
    layer.drawableSize = drawable_size;
  }

  // Flutter needs to read from the color attachment in cases where there are effects such as
  // backdrop filters. Flutter plugins that create platform views may also read from the layer.
  layer.framebufferOnly = NO;

  return (__bridge GPUCAMetalLayerHandle)layer;
}

// |GPUSurfaceMetalDelegate|
bool IOSSurfaceMetalImpeller::PresentDrawable(GrMTLHandle drawable) const {
  FML_DCHECK(false);
  return false;
}

// |GPUSurfaceMetalDelegate|
GPUMTLTextureInfo IOSSurfaceMetalImpeller::GetMTLTexture(const DlISize& frame_info) const {
  FML_CHECK(false);
  return GPUMTLTextureInfo{
      .texture_id = -1,   //
      .texture = nullptr  //
  };
}

// |GPUSurfaceMetalDelegate|
bool IOSSurfaceMetalImpeller::PresentTexture(GPUMTLTextureInfo texture) const {
  FML_CHECK(false);
  return false;
}

// |GPUSurfaceMetalDelegate|
bool IOSSurfaceMetalImpeller::AllowsDrawingWhenGpuDisabled() const {
  return false;
}

}  // namespace flutter
