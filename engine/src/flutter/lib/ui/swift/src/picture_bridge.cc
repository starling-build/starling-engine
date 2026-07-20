// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/picture_bridge.h"
#include "include/canvas_bridge.h"
#include "include/image_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/display_list.h"
#include "flutter/display_list/dl_builder.h"
#include "flutter/display_list/image/dl_image.h"
#include "flutter/display_list/skia/dl_sk_dispatcher.h"
#include "third_party/skia/include/core/SkSurface.h"

namespace flutter::swift_bridge {

// Pimpl implementation holding the actual Flutter types.
struct PictureImpl {
  sk_sp<flutter::DisplayList> display_list;

  explicit PictureImpl(sk_sp<flutter::DisplayList> dl)
      : display_list(std::move(dl)) {}
};

PictureBridge::PictureBridge(const CanvasBridge* canvas_bridge) {
  // Get the DisplayListBuilder pointer from the canvas bridge.
  // This returns a pointer to the sk_sp<DisplayListBuilder>.
  const void* builder_ptr =
      canvas_bridge ? canvas_bridge->GetDisplayListBuilderPtr() : nullptr;

  if (builder_ptr) {
    // Cast to the actual type and call Build() to get the DisplayList.
    const auto& builder =
        *static_cast<const sk_sp<flutter::DisplayListBuilder>*>(builder_ptr);
    if (builder) {
      sk_sp<flutter::DisplayList> dl = builder->Build();
      impl_ = new PictureImpl(std::move(dl));
    } else {
      impl_ = new PictureImpl(nullptr);
    }
  } else {
    impl_ = new PictureImpl(nullptr);
  }
}

PictureBridge::~PictureBridge() {
  delete impl_;
}

int64_t PictureBridge::GetAllocationSize() const {
  if (impl_ && impl_->display_list) {
    return static_cast<int64_t>(impl_->display_list->bytes() +
                                sizeof(PictureBridge));
  }
  return static_cast<int64_t>(sizeof(PictureBridge));
}

const void* PictureBridge::GetDisplayListPtr() const {
  if (!impl_ || !impl_->display_list) {
    return nullptr;
  }
  return static_cast<const void*>(&impl_->display_list);
}

void PictureBridge::Dispose() {
  if (impl_) {
    impl_->display_list.reset();
  }
}

ImageBridge* PictureBridge::ToImage(int width, int height) const {
  if (!impl_ || !impl_->display_list) {
    return nullptr;
  }

  // Create a raster SkSurface of the requested dimensions.
  auto surface = SkSurfaces::Raster(
      SkImageInfo::Make(width, height, kRGBA_8888_SkColorType,
                        kPremul_SkAlphaType, nullptr));
  if (!surface) {
    return nullptr;
  }

  // Replay the DisplayList onto the surface's canvas via DlSkCanvasDispatcher.
  auto* canvas = surface->getCanvas();
  DlSkCanvasDispatcher dispatcher(canvas);
  impl_->display_list->Dispatch(dispatcher);

  // Snapshot the surface to get an SkImage.
  sk_sp<SkImage> sk_image = surface->makeImageSnapshot();
  if (!sk_image) {
    return nullptr;
  }

  // Wrap in DlImage, heap-allocate for SetDlImageFromPtr ownership transfer.
  auto dl_image = DlImage::Make(std::move(sk_image));
  auto* dl_image_ptr = new sk_sp<flutter::DlImage>(std::move(dl_image));

  // Create ImageBridge and hand it the DlImage.
  auto* image_bridge = new ImageBridge();
  image_bridge->SetDlImageFromPtr(static_cast<void*>(dl_image_ptr));
  return image_bridge;
}

bool PictureBridge::IsDisposed() const {
  return !impl_ || !impl_->display_list;
}

}  // namespace flutter::swift_bridge
