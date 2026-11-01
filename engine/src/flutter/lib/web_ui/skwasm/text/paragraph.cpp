// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/skia/modules/skparagraph/include/Paragraph.h"
#include "../export.h"
#include "../live_objects.h"
#include "DartTypes.h"
#include "TextStyle.h"
#include "include/core/SkScalar.h"
#include "include/core/SkPath.h"
#include "text_types.h"

#include <cstring>
#include <vector>

using namespace Skwasm;

SKWASM_EXPORT void paragraph_dispose(Paragraph* paragraph) {
  liveParagraphCount--;
  delete paragraph;
}

SKWASM_EXPORT SkScalar paragraph_getWidth(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getMaxWidth();
}

SKWASM_EXPORT SkScalar paragraph_getHeight(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getHeight();
}

SKWASM_EXPORT bool paragraph_getGlyphInkBounds(Paragraph* paragraph,
                                               SkRect* bounds) {
  if (!paragraph || !bounds) {
    return false;
  }
  *bounds = SkRect::MakeEmpty();
  paragraph->skiaParagraph->extendedVisit(
      [&](int, const skia::textlayout::Paragraph::ExtendedVisitorInfo* info) {
        if (!info) {
          return;
        }
        for (int i = 0; i < info->count; ++i) {
          const auto path = info->font.getPath(info->glyphs[i]);
          const auto ink = path ? path->computeTightBounds() : info->bounds[i];
          if (!ink.isEmpty()) {
            bounds->join(ink.makeOffset(
                info->origin.x() + info->positions[i].x(),
                info->origin.y() + info->positions[i].y()));
          }
        }
      });
  return !bounds->isEmpty() && bounds->isFinite();
}

// (Starling: the outlines of the glyphs whose cluster starts in the UTF-8 byte
// range [start, end), in paragraph coordinates, flattened the way the native
// bridge's ParagraphBridge::GetGlyphPath does: verbs (0 move, 1 line, 2 quad,
// 4 cubic, 5 close — Skia's numbers; a conic comes back as quads) and points
// (x, y pairs). counts[0] and counts[1] are set to the verbs and points the
// outline needs; the buffers are filled only if they hold them (call once with
// null buffers to size them). False for no outline. Absent from Google's
// published skwasm; the host binds it to nothing there.)
SKWASM_EXPORT bool paragraph_getGlyphPath(Paragraph* paragraph,
                                          uint32_t start,
                                          uint32_t end,
                                          uint8_t* verbs,
                                          uint32_t verbCapacity,
                                          float* points,
                                          uint32_t pointCapacity,
                                          uint32_t* counts) {
  if (counts) {
    counts[0] = 0;
    counts[1] = 0;
  }
  if (!paragraph || end <= start) {
    return false;
  }
  SkPath all;
  paragraph->skiaParagraph->extendedVisit(
      [&](int, const skia::textlayout::Paragraph::ExtendedVisitorInfo* info) {
        if (!info) {
          return;
        }
        for (int i = 0; i < info->count; ++i) {
          if (info->utf8Starts[i] < start || info->utf8Starts[i] >= end) {
            continue;
          }
          const auto path = info->font.getPath(info->glyphs[i]);
          if (!path || path->isEmpty()) {
            continue;
          }
          all.addPath(*path, info->origin.x() + info->positions[i].x(),
                      info->origin.y() + info->positions[i].y());
        }
      });
  if (all.isEmpty()) {
    return false;
  }
  std::vector<uint8_t> outVerbs;
  std::vector<float> outPoints;
  auto add = [&](const SkPoint& p) {
    outPoints.push_back(p.fX);
    outPoints.push_back(p.fY);
  };
  SkPath::Iter iter(all, false);
  SkPoint pts[4];
  for (SkPath::Verb verb = iter.next(pts); verb != SkPath::kDone_Verb;
       verb = iter.next(pts)) {
    switch (verb) {
      case SkPath::kMove_Verb:
        outVerbs.push_back(0);
        add(pts[0]);
        break;
      case SkPath::kLine_Verb:
        outVerbs.push_back(1);
        add(pts[1]);
        break;
      case SkPath::kQuad_Verb:
        outVerbs.push_back(2);
        add(pts[1]);
        add(pts[2]);
        break;
      case SkPath::kConic_Verb: {
        SkPoint quads[1 + 2 * 4];
        const int n = SkPath::ConvertConicToQuads(pts[0], pts[1], pts[2],
                                                  iter.conicWeight(), quads, 2);
        for (int q = 0; q < n; ++q) {
          outVerbs.push_back(2);
          add(quads[1 + 2 * q]);
          add(quads[2 + 2 * q]);
        }
        break;
      }
      case SkPath::kCubic_Verb:
        outVerbs.push_back(4);
        add(pts[1]);
        add(pts[2]);
        add(pts[3]);
        break;
      case SkPath::kClose_Verb:
        outVerbs.push_back(5);
        break;
      default:
        break;
    }
  }
  if (counts) {
    counts[0] = static_cast<uint32_t>(outVerbs.size());
    counts[1] = static_cast<uint32_t>(outPoints.size() / 2);
  }
  if (verbs && points && outVerbs.size() <= verbCapacity &&
      outPoints.size() / 2 <= pointCapacity) {
    memcpy(verbs, outVerbs.data(), outVerbs.size());
    memcpy(points, outPoints.data(), outPoints.size() * sizeof(float));
  }
  return !outVerbs.empty();
}

SKWASM_EXPORT SkScalar paragraph_getLongestLine(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getLongestLine();
}

SKWASM_EXPORT SkScalar paragraph_getMinIntrinsicWidth(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getMinIntrinsicWidth();
}

SKWASM_EXPORT SkScalar paragraph_getMaxIntrinsicWidth(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getMaxIntrinsicWidth();
}

SKWASM_EXPORT SkScalar paragraph_getAlphabeticBaseline(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getAlphabeticBaseline();
}

SKWASM_EXPORT SkScalar paragraph_getIdeographicBaseline(Paragraph* paragraph) {
  return paragraph->skiaParagraph->getIdeographicBaseline();
}

SKWASM_EXPORT bool paragraph_getDidExceedMaxLines(Paragraph* paragraph) {
  return paragraph->skiaParagraph->didExceedMaxLines();
}

SKWASM_EXPORT void paragraph_layout(Paragraph* paragraph, SkScalar width) {
  paragraph->skiaParagraph->layout(width);
}

SKWASM_EXPORT int32_t
paragraph_getPositionForOffset(Paragraph* paragraph,
                               SkScalar offsetX,
                               SkScalar offsetY,
                               skia::textlayout::Affinity* outAffinity) {
  auto position =
      paragraph->skiaParagraph->getGlyphPositionAtCoordinate(offsetX, offsetY);
  if (outAffinity) {
    *outAffinity = position.affinity;
  }
  return position.position;
}

SKWASM_EXPORT bool paragraph_getClosestGlyphInfoAtCoordinate(
    Paragraph* paragraph,
    SkScalar offsetX,
    SkScalar offsetY,
    // Out parameters:
    SkRect* graphemeLayoutBounds,   // 1 SkRect
    size_t* graphemeCodeUnitRange,  // 2 size_ts: [start, end]
    bool* booleanFlags) {           // 1 boolean: isLTR
  skia::textlayout::Paragraph::GlyphInfo glyphInfo;
  if (!paragraph->skiaParagraph->getClosestUTF16GlyphInfoAt(offsetX, offsetY,
                                                            &glyphInfo)) {
    return false;
  }
  // This is more verbose than memcpying the whole struct but ideally we don't
  // want to depend on the exact memory layout of the struct.
  std::memcpy(graphemeLayoutBounds, &glyphInfo.fGraphemeLayoutBounds,
              sizeof(SkRect));
  std::memcpy(graphemeCodeUnitRange, &glyphInfo.fGraphemeClusterTextRange,
              2 * sizeof(size_t));
  booleanFlags[0] =
      glyphInfo.fDirection == skia::textlayout::TextDirection::kLtr;
  return true;
}

SKWASM_EXPORT bool paragraph_getGlyphInfoAt(
    Paragraph* paragraph,
    size_t index,
    // Out parameters:
    SkRect* graphemeLayoutBounds,   // 1 SkRect
    size_t* graphemeCodeUnitRange,  // 2 size_ts: [start, end]
    bool* booleanFlags) {           // 1 boolean: isLTR
  skia::textlayout::Paragraph::GlyphInfo glyphInfo;
  if (!paragraph->skiaParagraph->getGlyphInfoAtUTF16Offset(index, &glyphInfo)) {
    return false;
  }
  std::memcpy(graphemeLayoutBounds, &glyphInfo.fGraphemeLayoutBounds,
              sizeof(SkRect));
  std::memcpy(graphemeCodeUnitRange, &glyphInfo.fGraphemeClusterTextRange,
              2 * sizeof(size_t));
  booleanFlags[0] =
      glyphInfo.fDirection == skia::textlayout::TextDirection::kLtr;
  return true;
}

SKWASM_EXPORT void paragraph_getWordBoundary(
    Paragraph* paragraph,
    unsigned int position,
    int32_t* outRange  // Two `int32_t`s, start and end
) {
  auto range = paragraph->skiaParagraph->getWordBoundary(position);
  outRange[0] = range.start;
  outRange[1] = range.end;
}

SKWASM_EXPORT size_t paragraph_getLineCount(Paragraph* paragraph) {
  return paragraph->skiaParagraph->lineNumber();
}

SKWASM_EXPORT int paragraph_getLineNumberAt(Paragraph* paragraph,
                                            size_t characterIndex) {
  return paragraph->skiaParagraph->getLineNumberAtUTF16Offset(characterIndex);
}

SKWASM_EXPORT skia::textlayout::LineMetrics* paragraph_getLineMetricsAtIndex(
    Paragraph* paragraph,
    size_t lineNumber) {
  liveLineMetricsCount++;
  auto metrics = new skia::textlayout::LineMetrics();
  if (paragraph->skiaParagraph->getLineMetricsAt(lineNumber, metrics)) {
    return metrics;
  } else {
    delete metrics;
    return nullptr;
  }
}

struct TextBoxList {
  std::vector<skia::textlayout::TextBox> boxes;
};

SKWASM_EXPORT void textBoxList_dispose(TextBoxList* list) {
  liveTextBoxListCount--;
  delete list;
}

SKWASM_EXPORT size_t textBoxList_getLength(TextBoxList* list) {
  return list->boxes.size();
}

SKWASM_EXPORT skia::textlayout::TextDirection
textBoxList_getBoxAtIndex(TextBoxList* list, size_t index, SkRect* outRect) {
  const auto& box = list->boxes[index];
  *outRect = box.rect;
  return box.direction;
}

SKWASM_EXPORT TextBoxList* paragraph_getBoxesForRange(
    Paragraph* paragraph,
    int start,
    int end,
    skia::textlayout::RectHeightStyle heightStyle,
    skia::textlayout::RectWidthStyle widthStyle) {
  liveTextBoxListCount++;
  return new TextBoxList{paragraph->skiaParagraph->getRectsForRange(
      start, end, heightStyle, widthStyle)};
}

SKWASM_EXPORT TextBoxList* paragraph_getBoxesForPlaceholders(
    Paragraph* paragraph) {
  liveTextBoxListCount++;
  return new TextBoxList{paragraph->skiaParagraph->getRectsForPlaceholders()};
}

// Returns a list of the code points that were unable to be rendered with the
// selected fonts. The list is deduplicated, so each code point in the output
// is unique.
// If `nullptr` is passed in for `outCodePoints`, we simply return the count
// of the code points.
// Note: This must be called after the paragraph has been laid out at least
// once in order to get valid data.
SKWASM_EXPORT int paragraph_getUnresolvedCodePoints(Paragraph* paragraph,
                                                    SkUnichar* outCodePoints,
                                                    int outLength) {
  if (!outCodePoints) {
    return paragraph->skiaParagraph->unresolvedCodepoints().size();
  }
  int outIndex = 0;
  for (SkUnichar character : paragraph->skiaParagraph->unresolvedCodepoints()) {
    if (outIndex < outLength) {
      outCodePoints[outIndex] = character;
      outIndex++;
    } else {
      break;
    }
  }
  return outIndex;
}
