// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../export.h"
#include "../wrappers.h"
#include "text_types.h"
#include "third_party/skia/modules/skparagraph/include/Paragraph.h"

using namespace Skwasm;

SKWASM_EXPORT ParagraphStyle* paragraphStyle_create() {
  auto style = new ParagraphStyle();

  // This is the default behavior in Flutter
  style->skiaParagraphStyle.setReplaceTabCharacters(true);

  // Default text style has a black color
  style->textStyle.skiaStyle.setColor(SK_ColorBLACK);

  return style;
}

SKWASM_EXPORT void paragraphStyle_dispose(ParagraphStyle* style) {
  delete style;
}

SKWASM_EXPORT void paragraphStyle_setTextAlign(
    ParagraphStyle* style,
    skia::textlayout::TextAlign align) {
  style->skiaParagraphStyle.setTextAlign(align);
}

SKWASM_EXPORT void paragraphStyle_setTextDirection(
    ParagraphStyle* style,
    skia::textlayout::TextDirection direction) {
  style->skiaParagraphStyle.setTextDirection(direction);
}

SKWASM_EXPORT void paragraphStyle_setMaxLines(ParagraphStyle* style,
                                              size_t maxLines) {
  style->skiaParagraphStyle.setMaxLines(maxLines);
}

SKWASM_EXPORT void paragraphStyle_setHeight(ParagraphStyle* style,
                                            SkScalar height) {
  style->skiaParagraphStyle.setHeight(height);
}

SKWASM_EXPORT void paragraphStyle_setTextHeightBehavior(
    ParagraphStyle* style,
    bool applyHeightToFirstAscent,
    bool applyHeightToLastDescent) {
  skia::textlayout::TextHeightBehavior behavior;
  if (!applyHeightToFirstAscent && !applyHeightToLastDescent) {
    behavior = skia::textlayout::kDisableAll;
  } else if (!applyHeightToLastDescent) {
    behavior = skia::textlayout::kDisableLastDescent;
  } else if (!applyHeightToFirstAscent) {
    behavior = skia::textlayout::kDisableFirstAscent;
  } else {
    behavior = skia::textlayout::kAll;
  }
  style->skiaParagraphStyle.setTextHeightBehavior(behavior);
}

SKWASM_EXPORT void paragraphStyle_setEllipsis(ParagraphStyle* style,
                                              SkString* ellipsis) {
  style->skiaParagraphStyle.setEllipsis(*ellipsis);
}

SKWASM_EXPORT void paragraphStyle_setStrutStyle(
    ParagraphStyle* style,
    skia::textlayout::StrutStyle* strutStyle) {
  style->skiaParagraphStyle.setStrutStyle(*strutStyle);
}

SKWASM_EXPORT void paragraphStyle_setTextStyle(ParagraphStyle* style,
                                               TextStyle* textStyle) {
  style->textStyle = *textStyle;
}

// (Starling: a line keeps its height's fraction, where skparagraph rounds
// it to a whole unit — patches/skia-fractional-line-heights.patch, which
// the native bridge reaches through bit 13 of the encoded style. Absent
// from Google's published skwasm; the host binds it to nothing there.)
SKWASM_EXPORT void paragraphStyle_setFractionalLineHeights(ParagraphStyle* style,
                                                           bool value) {
  style->skiaParagraphStyle.setFractionalLineHeights(value);
}

// (Starling: one more line's (left, right) inset, appended in line order; the
// last repeats for the lines after it — skparagraph's setLineIndents, through
// patches/skia-style-line-indents-metrics.patch, which the native bridge
// reaches through the encoded style's slots. Absent from Google's published
// skwasm; the host binds it to nothing there.)
SKWASM_EXPORT void paragraphStyle_addLineIndent(ParagraphStyle* style,
                                                SkScalar left,
                                                SkScalar right) {
  auto indents = style->skiaParagraphStyle.getLineIndents();
  indents.emplace_back(left, right);
  style->skiaParagraphStyle.setLineIndents(std::move(indents));
}

SKWASM_EXPORT void paragraphStyle_setApplyRoundingHack(ParagraphStyle* style,
                                                       bool applyRoundingHack) {
  style->skiaParagraphStyle.setApplyRoundingHack(applyRoundingHack);
}
