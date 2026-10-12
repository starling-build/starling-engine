// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Starling: PDF output from the web renderer. The counterpart of
// flutter/lib/ui/swift/src/pdf_bridge.cc — the same pages through the same
// Skia PDF backend — except that a tab has no file to write, so the document
// comes back as bytes for the page to hand to the browser as a download.

#include "export.h"
#include "live_objects.h"
#include "surface.h"

#include <unordered_map>

#include "flutter/display_list/display_list.h"
#include "flutter/display_list/image/dl_image.h"
#include "flutter/display_list/skia/dl_sk_dispatcher.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkDocument.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkString.h"
#include "third_party/skia/include/docs/SkPDFDocument.h"

using namespace flutter;

namespace {

// A picture the browser decoded is a texture in the surface's GL context,
// and the PDF backend, which has no GPU, would write it as nothing. Each
// such image is swapped for a raster copy on its way to the page.
// TODO(starling): an image inside a shader (a tiled fill) is not reached.
class PdfDispatcher : public DlSkCanvasDispatcher {
 public:
  // `copies` outlives the pages: a logo on every slide is copied once.
  using Copies = std::unordered_map<uint32_t, sk_sp<DlImage>>;
  PdfDispatcher(SkCanvas* canvas, Skwasm::Surface* surface, Copies* copies)
      : DlSkCanvasDispatcher(canvas), surface_(surface), copies_(*copies) {}

  void drawImage(const sk_sp<DlImage> image,
                 const DlPoint& point,
                 DlImageSampling sampling,
                 bool render_with_attributes) override {
    DlSkCanvasDispatcher::drawImage(raster(image), point, sampling,
                                    render_with_attributes);
  }

  void drawImageRect(const sk_sp<DlImage> image,
                     const DlRect& src,
                     const DlRect& dst,
                     DlImageSampling sampling,
                     bool render_with_attributes,
                     DlSrcRectConstraint constraint) override {
    DlSkCanvasDispatcher::drawImageRect(raster(image), src, dst, sampling,
                                        render_with_attributes, constraint);
  }

 private:
  sk_sp<DlImage> raster(const sk_sp<DlImage>& image) {
    sk_sp<SkImage> sk = image ? image->skia_image() : nullptr;
    if (!sk || surface_ == nullptr ||
        !(sk->isTextureBacked() || sk->isLazyGenerated())) {
      return image;
    }
    auto found = copies_.find(sk->uniqueID());
    if (found != copies_.end()) {
      return found->second;
    }
    // A picture-backed image rasterises without a GPU; a texture does not.
    sk_sp<SkImage> copy = sk->makeRasterImage(nullptr);
    if (!copy) {
      copy = surface_->rasterCopy(sk.get());
    }
    sk_sp<DlImage> result = copy ? DlImage::Make(std::move(copy)) : image;
    copies_[sk->uniqueID()] = result;
    return result;
  }

  Skwasm::Surface* surface_;
  Copies& copies_;
};

}  // namespace

// `surface` is the one whose context the pages' images live in, or null.
// `sizes` is count pairs of (width, height) in PDF points; a picture is
// played back at one unit per point. `title` and `author` may be null.
// Returns the document, to be released with skData_dispose, or null.
SKWASM_EXPORT SkData* pdf_write(Skwasm::Surface* surface,
                                DisplayList** pictures,
                                int count,
                                const float* sizes,
                                SkString* title,
                                SkString* author) {
  if (pictures == nullptr || sizes == nullptr || count <= 0) {
    return nullptr;
  }
  SkDynamicMemoryWStream stream;
  SkPDF::Metadata metadata;
  // This build has no JPEG encoder (tools/gn: skia_use_no_jpeg_encode), so
  // every bitmap is written deflated instead of as a DCT stream.
  metadata.allowNoJpegs = true;
  if (title != nullptr) {
    metadata.fTitle = *title;
  }
  if (author != nullptr) {
    metadata.fAuthor = *author;
  }
  metadata.fCreator = "Starling Office";
  metadata.fProducer = "Starling SDK / Skia PDF";
  auto doc = SkPDF::MakeDocument(&stream, metadata);
  if (!doc) {
    return nullptr;
  }
  PdfDispatcher::Copies copies;
  for (int i = 0; i < count; i++) {
    SkCanvas* canvas = doc->beginPage(sizes[2 * i], sizes[2 * i + 1]);
    if (canvas == nullptr) {
      return nullptr;
    }
    if (pictures[i] != nullptr) {
      PdfDispatcher dispatcher(canvas, surface, &copies);
      pictures[i]->Dispatch(dispatcher);
    }
    doc->endPage();
  }
  doc->close();
  liveDataCount++;
  return stream.detachAsData().release();
}
