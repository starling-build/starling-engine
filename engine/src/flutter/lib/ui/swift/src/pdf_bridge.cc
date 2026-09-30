// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/pdf_bridge.h"

#include "flutter/display_list/display_list.h"
#include "flutter/display_list/skia/dl_sk_dispatcher.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkDocument.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/docs/SkPDFDocument.h"
#include "third_party/skia/include/docs/SkPDFJpegHelpers.h"

namespace flutter::swift_bridge {

bool WritePdf(const char* path,
              const void* const* display_lists,
              int count,
              const double* widths,
              const double* heights,
              const char* title,
              const char* author) {
  if (path == nullptr || display_lists == nullptr || count <= 0 ||
      widths == nullptr || heights == nullptr) {
    return false;
  }
  SkFILEWStream stream(path);
  if (!stream.isValid()) {
    return false;
  }
  // Skia insists on JPEG callbacks (it re-encodes bitmaps as DCT streams).
  SkPDF::Metadata metadata = SkPDF::JPEG::MetadataWithCallbacks();
  if (title != nullptr) {
    metadata.fTitle = title;
  }
  if (author != nullptr) {
    metadata.fAuthor = author;
  }
  metadata.fCreator = "Starling Office";
  metadata.fProducer = "Starling SDK / Skia PDF";
  auto doc = SkPDF::MakeDocument(&stream, metadata);
  if (!doc) {
    return false;
  }
  for (int i = 0; i < count; i++) {
    SkCanvas* canvas = doc->beginPage(static_cast<SkScalar>(widths[i]),
                                      static_cast<SkScalar>(heights[i]));
    if (canvas == nullptr) {
      return false;
    }
    const auto* dl = static_cast<const sk_sp<DisplayList>*>(display_lists[i]);
    if (dl != nullptr && *dl) {
      DlSkCanvasDispatcher dispatcher(canvas);
      (*dl)->Dispatch(dispatcher);
    }
    doc->endPage();
  }
  doc->close();
  stream.flush();
  return true;
}

}  // namespace flutter::swift_bridge
