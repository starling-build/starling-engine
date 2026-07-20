// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/paragraph_bridge.h"
#include "include/canvas_bridge.h"

// Flutter engine headers (only in .cc file)
#include "flutter/display_list/dl_builder.h"
#include "flutter/txt/src/txt/paragraph.h"
#include "third_party/skia/modules/skparagraph/include/Paragraph.h"

#include <cmath>
#include <memory>
#include <vector>

namespace flutter::swift_bridge {

// Pimpl implementation holding the actual Flutter types.
struct ParagraphImpl {
  std::unique_ptr<txt::Paragraph> paragraph;

  explicit ParagraphImpl(std::unique_ptr<txt::Paragraph> p)
      : paragraph(std::move(p)) {}
};

ParagraphBridge::ParagraphBridge(void* paragraph_ptr) {
  if (paragraph_ptr) {
    // The caller passes a pointer to a std::unique_ptr<txt::Paragraph>
    // We take ownership by moving from it
    auto* unique_ptr =
        static_cast<std::unique_ptr<txt::Paragraph>*>(paragraph_ptr);
    impl_ = new ParagraphImpl(std::move(*unique_ptr));
  } else {
    impl_ = new ParagraphImpl(nullptr);
  }
}

ParagraphBridge::~ParagraphBridge() {
  delete impl_;
}

// MARK: - Metrics Getters

double ParagraphBridge::GetWidth() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetMaxWidth();
  }
  return 0.0;
}

double ParagraphBridge::GetHeight() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetHeight();
  }
  return 0.0;
}

double ParagraphBridge::GetLongestLine() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetLongestLine();
  }
  return 0.0;
}

double ParagraphBridge::GetMinIntrinsicWidth() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetMinIntrinsicWidth();
  }
  return 0.0;
}

double ParagraphBridge::GetMaxIntrinsicWidth() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetMaxIntrinsicWidth();
  }
  return 0.0;
}

double ParagraphBridge::GetAlphabeticBaseline() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetAlphabeticBaseline();
  }
  return 0.0;
}

double ParagraphBridge::GetIdeographicBaseline() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetIdeographicBaseline();
  }
  return 0.0;
}

bool ParagraphBridge::DidExceedMaxLines() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->DidExceedMaxLines();
  }
  return false;
}

size_t ParagraphBridge::GetNumberOfLines() const {
  if (impl_ && impl_->paragraph) {
    return impl_->paragraph->GetNumberOfLines();
  }
  return 0;
}

// MARK: - Layout

void ParagraphBridge::Layout(double width) {
  if (impl_ && impl_->paragraph) {
    impl_->paragraph->Layout(width);
  }
}

// MARK: - Box Queries

namespace {
// Helper to encode text boxes into a float array.
// Each box is 5 floats: left, top, right, bottom, direction
void EncodeTextBoxes(const std::vector<txt::Paragraph::TextBox>& boxes,
                     float* out_data,
                     size_t* out_count,
                     size_t max_boxes) {
  size_t count = std::min(boxes.size(), max_boxes);
  *out_count = count;

  size_t position = 0;
  for (size_t i = 0; i < count; ++i) {
    const txt::Paragraph::TextBox& box = boxes[i];
    out_data[position++] = box.rect.fLeft;
    out_data[position++] = box.rect.fTop;
    out_data[position++] = box.rect.fRight;
    out_data[position++] = box.rect.fBottom;
    out_data[position++] = static_cast<float>(box.direction);
  }
}
}  // namespace

void ParagraphBridge::GetBoxesForRange(unsigned start,
                                       unsigned end,
                                       unsigned box_height_style,
                                       unsigned box_width_style,
                                       float* out_data,
                                       size_t* out_count,
                                       size_t max_boxes) const {
  if (!impl_ || !impl_->paragraph) {
    *out_count = 0;
    return;
  }

  std::vector<txt::Paragraph::TextBox> boxes =
      impl_->paragraph->GetRectsForRange(
          start, end,
          static_cast<txt::Paragraph::RectHeightStyle>(box_height_style),
          static_cast<txt::Paragraph::RectWidthStyle>(box_width_style));

  EncodeTextBoxes(boxes, out_data, out_count, max_boxes);
}

size_t ParagraphBridge::GetBoxesForRangeCount(unsigned start,
                                              unsigned end,
                                              unsigned box_height_style,
                                              unsigned box_width_style) const {
  if (!impl_ || !impl_->paragraph) {
    return 0;
  }

  std::vector<txt::Paragraph::TextBox> boxes =
      impl_->paragraph->GetRectsForRange(
          start, end,
          static_cast<txt::Paragraph::RectHeightStyle>(box_height_style),
          static_cast<txt::Paragraph::RectWidthStyle>(box_width_style));

  return boxes.size();
}

void ParagraphBridge::GetBoxesForPlaceholders(float* out_data,
                                              size_t* out_count,
                                              size_t max_boxes) const {
  if (!impl_ || !impl_->paragraph) {
    *out_count = 0;
    return;
  }

  std::vector<txt::Paragraph::TextBox> boxes =
      impl_->paragraph->GetRectsForPlaceholders();

  EncodeTextBoxes(boxes, out_data, out_count, max_boxes);
}

size_t ParagraphBridge::GetBoxesForPlaceholdersCount() const {
  if (!impl_ || !impl_->paragraph) {
    return 0;
  }

  return impl_->paragraph->GetRectsForPlaceholders().size();
}

// MARK: - Hit Testing

void ParagraphBridge::GetPositionForOffset(double dx,
                                           double dy,
                                           size_t* out_offset,
                                           int* out_affinity) const {
  if (!impl_ || !impl_->paragraph) {
    *out_offset = 0;
    *out_affinity = 1;  // downstream
    return;
  }

  txt::Paragraph::PositionWithAffinity pos =
      impl_->paragraph->GetGlyphPositionAtCoordinate(dx, dy);
  *out_offset = pos.position;
  *out_affinity = static_cast<int>(pos.affinity);
}

void ParagraphBridge::GetWordBoundary(unsigned offset,
                                      size_t* out_start,
                                      size_t* out_end) const {
  if (!impl_ || !impl_->paragraph) {
    *out_start = 0;
    *out_end = 0;
    return;
  }

  txt::Paragraph::Range<size_t> boundary =
      impl_->paragraph->GetWordBoundary(offset);
  *out_start = boundary.start;
  *out_end = boundary.end;
}

void ParagraphBridge::GetLineBoundary(unsigned offset,
                                      int* out_start,
                                      int* out_end) const {
  if (!impl_ || !impl_->paragraph) {
    *out_start = -1;
    *out_end = -1;
    return;
  }

  // Same logic as paragraph.cc:getLineBoundary
  std::vector<txt::LineMetrics> metrics = impl_->paragraph->GetLineMetrics();
  int line_start = -1;
  int line_end = -1;

  for (const txt::LineMetrics& line : metrics) {
    if (offset >= line.start_index && offset <= line.end_index) {
      line_start = static_cast<int>(line.start_index);
      line_end = static_cast<int>(line.end_index);
      break;
    }
  }

  *out_start = line_start;
  *out_end = line_end;
}

// MARK: - Glyph Info

bool ParagraphBridge::GetGlyphInfoAt(unsigned code_unit_offset,
                                     double* out_bounds_left,
                                     double* out_bounds_top,
                                     double* out_bounds_right,
                                     double* out_bounds_bottom,
                                     size_t* out_range_start,
                                     size_t* out_range_end,
                                     bool* out_is_ltr) const {
  if (!impl_ || !impl_->paragraph) {
    return false;
  }

  skia::textlayout::Paragraph::GlyphInfo glyph_info;
  bool found = impl_->paragraph->GetGlyphInfoAt(code_unit_offset, &glyph_info);

  if (!found) {
    return false;
  }

  *out_bounds_left = glyph_info.fGraphemeLayoutBounds.fLeft;
  *out_bounds_top = glyph_info.fGraphemeLayoutBounds.fTop;
  *out_bounds_right = glyph_info.fGraphemeLayoutBounds.fRight;
  *out_bounds_bottom = glyph_info.fGraphemeLayoutBounds.fBottom;
  *out_range_start = glyph_info.fGraphemeClusterTextRange.start;
  *out_range_end = glyph_info.fGraphemeClusterTextRange.end;
  *out_is_ltr =
      (glyph_info.fDirection == skia::textlayout::TextDirection::kLtr);

  return true;
}

bool ParagraphBridge::GetClosestGlyphInfoForOffset(double dx,
                                                   double dy,
                                                   double* out_bounds_left,
                                                   double* out_bounds_top,
                                                   double* out_bounds_right,
                                                   double* out_bounds_bottom,
                                                   size_t* out_range_start,
                                                   size_t* out_range_end,
                                                   bool* out_is_ltr) const {
  if (!impl_ || !impl_->paragraph) {
    return false;
  }

  skia::textlayout::Paragraph::GlyphInfo glyph_info;
  bool found =
      impl_->paragraph->GetClosestGlyphInfoAtCoordinate(dx, dy, &glyph_info);

  if (!found) {
    return false;
  }

  *out_bounds_left = glyph_info.fGraphemeLayoutBounds.fLeft;
  *out_bounds_top = glyph_info.fGraphemeLayoutBounds.fTop;
  *out_bounds_right = glyph_info.fGraphemeLayoutBounds.fRight;
  *out_bounds_bottom = glyph_info.fGraphemeLayoutBounds.fBottom;
  *out_range_start = glyph_info.fGraphemeClusterTextRange.start;
  *out_range_end = glyph_info.fGraphemeClusterTextRange.end;
  *out_is_ltr =
      (glyph_info.fDirection == skia::textlayout::TextDirection::kLtr);

  return true;
}

// MARK: - Line Metrics

void ParagraphBridge::ComputeLineMetrics(double* out_data,
                                         size_t* out_count,
                                         size_t max_lines) const {
  if (!impl_ || !impl_->paragraph) {
    *out_count = 0;
    return;
  }

  std::vector<txt::LineMetrics> metrics = impl_->paragraph->GetLineMetrics();
  size_t count = std::min(metrics.size(), max_lines);
  *out_count = count;

  // Each line is 9 doubles: hard_break, ascent, descent, unscaled_ascent,
  // height, width, left, baseline, line_number
  // Same layout as paragraph.cc:computeLineMetrics
  size_t position = 0;
  for (size_t i = 0; i < count; ++i) {
    const txt::LineMetrics& line = metrics[i];
    out_data[position++] = static_cast<double>(line.hard_break);
    out_data[position++] = line.ascent;
    out_data[position++] = line.descent;
    out_data[position++] = line.unscaled_ascent;
    // Height = round(ascent + descent), matching paragraph.cc
    out_data[position++] = round(line.ascent + line.descent);
    out_data[position++] = line.width;
    out_data[position++] = line.left;
    out_data[position++] = line.baseline;
    out_data[position++] = static_cast<double>(line.line_number);
  }
}

size_t ParagraphBridge::ComputeLineMetricsCount() const {
  if (!impl_ || !impl_->paragraph) {
    return 0;
  }

  return impl_->paragraph->GetLineMetrics().size();
}

bool ParagraphBridge::GetLineMetricsAt(int line_number,
                                       bool* out_hard_break,
                                       double* out_ascent,
                                       double* out_descent,
                                       double* out_unscaled_ascent,
                                       double* out_height,
                                       double* out_width,
                                       double* out_left,
                                       double* out_baseline,
                                       int* out_line_number) const {
  if (!impl_ || !impl_->paragraph) {
    return false;
  }

  skia::textlayout::LineMetrics line;
  bool found = impl_->paragraph->GetLineMetricsAt(line_number, &line);

  if (!found) {
    return false;
  }

  *out_hard_break = line.fHardBreak;
  *out_ascent = line.fAscent;
  *out_descent = line.fDescent;
  *out_unscaled_ascent = line.fUnscaledAscent;
  // Height = round(ascent + descent), matching paragraph.cc
  *out_height = round(line.fAscent + line.fDescent);
  *out_width = line.fWidth;
  *out_left = line.fLeft;
  *out_baseline = line.fBaseline;
  *out_line_number = static_cast<int>(line.fLineNumber);

  return true;
}

int ParagraphBridge::GetLineNumberAt(size_t code_unit_offset) const {
  if (!impl_ || !impl_->paragraph) {
    return -1;
  }

  return impl_->paragraph->GetLineNumberAt(code_unit_offset);
}

// MARK: - Painting

void ParagraphBridge::Paint(CanvasBridge* canvas_bridge, double x, double y) {
  if (!impl_ || !impl_->paragraph || !canvas_bridge) {
    return;
  }

  // Get the DisplayListBuilder from the canvas bridge
  const void* builder_ptr = canvas_bridge->GetDisplayListBuilderPtr();
  if (!builder_ptr) {
    return;
  }

  // Cast to the actual type
  const auto& builder =
      *static_cast<const sk_sp<flutter::DisplayListBuilder>*>(builder_ptr);
  if (!builder) {
    return;
  }

  // Paint the paragraph onto the display list builder
  impl_->paragraph->Paint(builder.get(), x, y);
}

// MARK: - Lifecycle

void ParagraphBridge::Dispose() {
  if (impl_) {
    impl_->paragraph.reset();
  }
}

bool ParagraphBridge::IsDisposed() const {
  return !impl_ || !impl_->paragraph;
}

}  // namespace flutter::swift_bridge
