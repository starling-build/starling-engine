// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_IMPELLER_DISPLAY_LIST_DL_TEXT_IMPELLER_H_
#define FLUTTER_IMPELLER_DISPLAY_LIST_DL_TEXT_IMPELLER_H_

#include "flutter/display_list/dl_text.h"
#include "flutter/impeller/typographer/text_frame.h"
#include "third_party/skia/include/core/SkTextBlob.h"

namespace flutter {
class DlTextImpeller : public DlText {
 public:
  static std::shared_ptr<DlTextImpeller> Make(
      const std::shared_ptr<impeller::TextFrame>& frame);
  // Builds the frame from `blob` and keeps the blob. Starling: Impeller draws
  // from the frame, but a display list replayed into a Skia canvas (Skia's PDF
  // backend, for export) needs the blob, and GetTextBlob() used to answer
  // nullptr, which the Skia dispatcher treats as fatal.
  static std::shared_ptr<DlTextImpeller> MakeFromBlob(
      const sk_sp<SkTextBlob>& blob);

  ~DlTextImpeller() = default;

  explicit DlTextImpeller(const std::shared_ptr<impeller::TextFrame>& frame);
  DlTextImpeller(const std::shared_ptr<impeller::TextFrame>& frame,
                 const sk_sp<SkTextBlob>& blob);

  DlRect GetBounds() const { return frame_->GetBounds(); }

  std::shared_ptr<impeller::TextFrame> GetTextFrame() const { return frame_; }

  const SkTextBlob* GetTextBlob() const { return blob_.get(); }

 private:
  std::shared_ptr<impeller::TextFrame> frame_;
  sk_sp<SkTextBlob> blob_;

  FML_DISALLOW_COPY_AND_ASSIGN(DlTextImpeller);
};
}  // namespace flutter

#endif  // FLUTTER_IMPELLER_DISPLAY_LIST_DL_TEXT_IMPELLER_H_
