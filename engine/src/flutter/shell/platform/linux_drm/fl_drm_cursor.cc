// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_cursor.h"

#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace flutter {

namespace {

// All cursor bitmaps are encoded as fixed-width ASCII rows.
// 'B' = black (outline), 'W' = white (fill), ' ' = transparent.
// The bitmap is blitted into the top-left of the 64×64 cursor buffer.

// Classic Windows-style arrow (12 cols × 21 rows). Hot-spot is (0, 0).
static const char* kBitmapDefault[] = {
    "B           ",
    "BB          ",
    "BWB         ",
    "BWWB        ",
    "BWWWB       ",
    "BWWWWB      ",
    "BWWWWWB     ",
    "BWWWWWWB    ",
    "BWWWWWWWB   ",
    "BWWWWWWWWB  ",
    "BWWWWWWWWWB ",
    "BWWWWWWWWWWB",
    "BWWWWWWBBBBB",
    "BWWBWWB     ",
    "BWBBWWWB    ",
    "BB  BWWB    ",
    "B    BWWB   ",
    "     BWWB   ",
    "      BWWB  ",
    "       BWB  ",
    "       BB   ",
};

// Vertical double arrow ↕ (17 cols × 23 rows).
static const char* kBitmapResizeNS[] = {
    "        B        ",
    "       BWB       ",
    "      BWWWB      ",
    "     BWWWWWB     ",
    "    BWWWWWWWB    ",
    "   BWWWWWWWWWB   ",
    "   BBBBBWBBBBB   ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "       BWB       ",
    "   BBBBBWBBBBB   ",
    "   BWWWWWWWWWB   ",
    "    BWWWWWWWB    ",
    "     BWWWWWB     ",
    "      BWWWB      ",
    "       BWB       ",
    "        B        ",
};

// Horizontal double arrow ↔ (23 cols × 17 rows).
static const char* kBitmapResizeEW[] = {
    "                       ",
    "                       ",
    "                       ",
    "    B             B    ",
    "   BWB           BWB   ",
    "  BWWB           BWWB  ",
    " BWWWBBBBBBBBBBBBBWWWB ",
    "BWWWWWWWWWWWWWWWWWWWWWB",
    "BWWWWWWWWWWWWWWWWWWWWWB",
    "BWWWWWWWWWWWWWWWWWWWWWB",
    "BWWWWWWWWWWWWWWWWWWWWWB",
    "BWWWWWWWWWWWWWWWWWWWWWB",
    " BWWWBBBBBBBBBBBBBWWWB ",
    "  BWWB           BWWB  ",
    "   BWB           BWB   ",
    "    B             B    ",
    "                       ",
};

// Diagonal ↗↙ (top-right / bottom-left), 19 cols × 19 rows.
// Two right-triangle arrowheads: tip at the top-right corner and tip at
// the bottom-left corner. Each triangle is 9×9 and they are separated by
// an empty diagonal gap.
static const char* kBitmapResizeNESW[] = {
    "          BBBBBBBBB",
    "           BWWWWWWB",
    "            BWWWWWB",
    "             BWWWWB",
    "              BWWWB",
    "               BWWB",
    "                BWB",
    "                 BB",
    "                  B",
    "                   ",
    "B                  ",
    "BB                 ",
    "BWB                ",
    "BWWB               ",
    "BWWWB              ",
    "BWWWWB             ",
    "BWWWWWB            ",
    "BWWWWWWB           ",
    "BBBBBBBBB          ",
};

// Diagonal ↖↘ (top-left / bottom-right), 19 cols × 19 rows.
// Mirror of the NESW bitmap.
static const char* kBitmapResizeNWSE[] = {
    "BBBBBBBBB          ",
    "BWWWWWWB           ",
    "BWWWWWB            ",
    "BWWWWB             ",
    "BWWWB              ",
    "BWWB               ",
    "BWB                ",
    "BB                 ",
    "B                  ",
    "                   ",
    "                  B",
    "                 BB",
    "                BWB",
    "               BWWB",
    "              BWWWB",
    "             BWWWWB",
    "            BWWWWWB",
    "           BWWWWWWB",
    "          BBBBBBBBB",
};

// I-beam ⌶ for text editing (9 cols × 17 rows). Hot-spot mid-stem (4, 8).
static const char* kBitmapText[] = {
    "BBBBBBBBB",
    "BWWWWWWWB",
    "BBBBWBBBB",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "   BWB   ",
    "BBBBWBBBB",
    "BWWWWWWWB",
    "BBBBBBBBB",
};

// Pointing hand for links (13 cols × 18 rows). Hot-spot at the index
// fingertip (5, 0).
static const char* kBitmapPointer[] = {
    "    BB       ",
    "   BWWB      ",
    "   BWWB      ",
    "   BWWB      ",
    "   BWWBBB    ",
    "   BWWBWWBB  ",
    "   BWWBWWBWBB",
    "   BWWWWWWBWB",
    "BB BWWWWWWWWB",
    "BWBBWWWWWWWWB",
    "BWWBWWWWWWWWB",
    " BWWWWWWWWWWB",
    "  BWWWWWWWWWB",
    "  BWWWWWWWWB ",
    "   BWWWWWWWB ",
    "   BWWWWWWWB ",
    "    BWWWWWB  ",
    "    BBBBBBB  ",
};

struct BitmapDef {
  const char* const* rows;
  uint32_t cols;
  uint32_t rows_count;
  // Hot-spot: the pixel inside the bitmap that sits on the pointer position.
  int hot_x;
  int hot_y;
};

BitmapDef BitmapForShape(FlCursorShape shape) {
  switch (shape) {
    case FlCursorShape::kResizeNS:
      return {kBitmapResizeNS, 17, 23, 8, 11};
    case FlCursorShape::kResizeEW:
      return {kBitmapResizeEW, 23, 17, 11, 8};
    case FlCursorShape::kResizeNESW:
      return {kBitmapResizeNESW, 19, 19, 9, 9};
    case FlCursorShape::kResizeNWSE:
      return {kBitmapResizeNWSE, 19, 19, 9, 9};
    case FlCursorShape::kText:
      return {kBitmapText, 9, 17, 4, 8};
    case FlCursorShape::kPointer:
      return {kBitmapPointer, 13, 18, 5, 0};
    case FlCursorShape::kDefault:
    default:
      return {kBitmapDefault, 12, 21, 0, 0};
  }
}

}  // namespace

FlDrmCursor::FlDrmCursor() = default;

FlDrmCursor::~FlDrmCursor() {
  if (cursor_bo_) {
    // Hide cursor before destroying buffer.
    if (visible_) {
      drmModeSetCursor(drm_fd_, crtc_id_, 0, 0, 0);
    }
    gbm_bo_destroy(cursor_bo_);
    cursor_bo_ = nullptr;
  }
}

bool FlDrmCursor::Initialize(int drm_fd, uint32_t crtc_id,
                               gbm_device* gbm_device) {
  drm_fd_ = drm_fd;
  crtc_id_ = crtc_id;

  cursor_bo_ = gbm_bo_create(gbm_device, kCursorWidth, kCursorHeight,
                               GBM_FORMAT_ARGB8888,
                               GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
  if (!cursor_bo_) {
    fprintf(stderr, "[Cursor] gbm_bo_create failed\n");
    return false;
  }

  LoadShape(FlCursorShape::kDefault);

  fprintf(stderr, "[Cursor] Hardware cursor initialized\n");
  return true;
}

void FlDrmCursor::LoadShape(FlCursorShape shape) {
  if (!cursor_bo_) {
    return;
  }
  BitmapDef def = BitmapForShape(shape);

  uint32_t buf[kCursorWidth * kCursorHeight];
  memset(buf, 0, sizeof(buf));

  // Top-left blit, clipped to the cursor buffer.
  uint32_t max_rows = def.rows_count < kCursorHeight ? def.rows_count
                                                     : kCursorHeight;
  uint32_t max_cols = def.cols < kCursorWidth ? def.cols : kCursorWidth;
  for (uint32_t y = 0; y < max_rows; y++) {
    for (uint32_t x = 0; x < max_cols; x++) {
      char c = def.rows[y][x];
      uint32_t color = 0x00000000;
      if (c == 'B') color = 0xFF000000;
      else if (c == 'W') color = 0xFFFFFFFF;
      buf[y * kCursorWidth + x] = color;
    }
  }

  gbm_bo_write(cursor_bo_, buf, sizeof(buf));
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    current_shape_ = shape;
    hot_x_ = def.hot_x;
    hot_y_ = def.hot_y;
    // Drop the guest's image: a snapshot taken from here on is a baked shape
    // and must not carry a picture that is no longer on the plane.
    custom_rgba_.reset();
  }
}

void FlDrmCursor::SetShape(FlCursorShape shape) {
  if (shape == current_shape_) {
    return;
  }
  fprintf(stderr, "[Cursor] shape -> %d\n", static_cast<int>(shape));
  LoadShape(shape);
  // Re-bind the (now-updated) buffer to the CRTC so the new pixels take
  // effect immediately, even when the cursor isn't moving, and re-anchor
  // the plane for the new hot-spot.
  if (visible_ && cursor_bo_) {
    uint32_t handle = gbm_bo_get_handle(cursor_bo_).u32;
    drmModeSetCursor(drm_fd_, crtc_id_, handle, kCursorWidth, kCursorHeight);
    drmModeMoveCursor(drm_fd_, crtc_id_, last_x_ - hot_x_, last_y_ - hot_y_);
  }
}

void FlDrmCursor::SetImage(const uint8_t* bgra, int width, int height,
                           int hot_x, int hot_y) {
  if (!cursor_bo_) {
    return;
  }

  uint32_t buf[kCursorWidth * kCursorHeight];
  memset(buf, 0, sizeof(buf));
  auto rgba = std::make_shared<std::vector<uint8_t>>(
      kCursorWidth * kCursorHeight * 4, 0);

  // width/height 0 means "no cursor": both buffers stay transparent, which
  // hides the sprite without taking the plane down.
  if (bgra && width > 0 && height > 0) {
    const uint32_t max_rows = static_cast<uint32_t>(height) < kCursorHeight
                                  ? static_cast<uint32_t>(height)
                                  : kCursorHeight;
    const uint32_t max_cols = static_cast<uint32_t>(width) < kCursorWidth
                                  ? static_cast<uint32_t>(width)
                                  : kCursorWidth;
    for (uint32_t y = 0; y < max_rows; y++) {
      for (uint32_t x = 0; x < max_cols; x++) {
        const uint8_t* src =
            bgra + (static_cast<size_t>(y) * static_cast<size_t>(width) + x) * 4;
        const uint32_t b = src[0], g = src[1], r = src[2], a = src[3];
        // The plane's default blend mode is pre-multiplied and the source is
        // straight alpha. The baked shapes never had to think about this —
        // they are alpha 0 or 255 — but a real cursor has soft edges, and
        // skipping this haloes every one of them.
        buf[y * kCursorWidth + x] = (a << 24) | (((r * a + 127) / 255) << 16) |
                                    (((g * a + 127) / 255) << 8) |
                                    ((b * a + 127) / 255);
        // The software copy stays straight-alpha: both capture paths blend
        // it that way (DrawCursorGpu sets GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA).
        uint8_t* dst = rgba->data() + (y * kCursorWidth + x) * 4;
        dst[0] = static_cast<uint8_t>(r);
        dst[1] = static_cast<uint8_t>(g);
        dst[2] = static_cast<uint8_t>(b);
        dst[3] = static_cast<uint8_t>(a);
      }
    }
  }

  gbm_bo_write(cursor_bo_, buf, sizeof(buf));

  int px, py;
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    current_shape_ = FlCursorShape::kCustom;
    // Clamped to the plane: a hot-spot outside it would anchor the sprite
    // somewhere the pointer is not.
    hot_x_ = hot_x < 0 ? 0
                       : (hot_x >= static_cast<int>(kCursorWidth)
                              ? static_cast<int>(kCursorWidth) - 1
                              : hot_x);
    hot_y_ = hot_y < 0 ? 0
                       : (hot_y >= static_cast<int>(kCursorHeight)
                              ? static_cast<int>(kCursorHeight) - 1
                              : hot_y);
    custom_rgba_ = rgba;
    image_gen_++;
    px = last_x_ - hot_x_;
    py = last_y_ - hot_y_;
  }

  // Re-bind and re-anchor exactly as SetShape does: the pixels must take
  // effect without waiting for motion, and every image brings its own
  // hot-spot, which the legacy cursor API cannot express any other way.
  if (visible_ && cursor_bo_) {
    uint32_t handle = gbm_bo_get_handle(cursor_bo_).u32;
    drmModeSetCursor(drm_fd_, crtc_id_, handle, kCursorWidth, kCursorHeight);
    drmModeMoveCursor(drm_fd_, crtc_id_, px, py);
  }
}

void FlDrmCursor::MoveTo(uint32_t crtc_id, int x, int y) {
  if (crtc_id != 0 && crtc_id != crtc_id_) {
    // Pointer crossed outputs: move the cursor plane to the new CRTC.
    if (visible_) {
      drmModeSetCursor(drm_fd_, crtc_id_, 0, 0, 0);
    }
    {
      std::lock_guard<std::mutex> lk(state_mu_);
      crtc_id_ = crtc_id;
    }
    if (visible_ && cursor_bo_) {
      uint32_t handle = gbm_bo_get_handle(cursor_bo_).u32;
      drmModeSetCursor(drm_fd_, crtc_id_, handle, kCursorWidth,
                       kCursorHeight);
    }
    fprintf(stderr, "[Cursor] -> crtc %u\n", crtc_id);
  }
  MoveTo(x, y);
}

void FlDrmCursor::MoveTo(int x, int y) {
  if (!visible_) {
    SetVisible(true);
  }
  int px, py;
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    last_x_ = x;
    last_y_ = y;
    // The legacy cursor API anchors the buffer's top-left at the given
    // position; subtract the shape's hot-spot so the logical click point
    // (arrow tip, I-beam center, fingertip) sits on the pointer.
    px = x - hot_x_;
    py = y - hot_y_;
  }
  drmModeMoveCursor(drm_fd_, crtc_id_, px, py);
}

void FlDrmCursor::Restore() {
  if (visible_ && cursor_bo_) {
    uint32_t handle = gbm_bo_get_handle(cursor_bo_).u32;
    drmModeSetCursor(drm_fd_, crtc_id_, handle, kCursorWidth, kCursorHeight);
  }
}

void FlDrmCursor::SetVisible(bool visible) {
  if (visible == visible_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lk(state_mu_);
    visible_ = visible;
  }
  if (visible && cursor_bo_) {
    uint32_t handle = gbm_bo_get_handle(cursor_bo_).u32;
    drmModeSetCursor(drm_fd_, crtc_id_, handle,
                      kCursorWidth, kCursorHeight);
  } else {
    drmModeSetCursor(drm_fd_, crtc_id_, 0, 0, 0);
  }
}

FlCursorSnapshot FlDrmCursor::Snapshot() const {
  std::lock_guard<std::mutex> lk(state_mu_);
  FlCursorSnapshot s;
  s.visible = visible_;
  s.crtc_id = crtc_id_;
  s.x = last_x_ - hot_x_;
  s.y = last_y_ - hot_y_;
  s.shape = current_shape_;
  s.image = custom_rgba_;
  s.image_gen = image_gen_;
  return s;
}

void FlDrmCursor::RenderShapeRGBA(FlCursorShape shape, uint8_t* rgba) {
  memset(rgba, 0, kCursorWidth * kCursorHeight * 4);
  BitmapDef def = BitmapForShape(shape);
  uint32_t max_rows = def.rows_count < kCursorHeight ? def.rows_count
                                                     : kCursorHeight;
  uint32_t max_cols = def.cols < kCursorWidth ? def.cols : kCursorWidth;
  for (uint32_t y = 0; y < max_rows; y++) {
    for (uint32_t x = 0; x < max_cols; x++) {
      char c = def.rows[y][x];
      if (c != 'B' && c != 'W') {
        continue;
      }
      uint8_t* p = rgba + (y * kCursorWidth + x) * 4;
      p[0] = p[1] = p[2] = (c == 'W') ? 0xFF : 0x00;
      p[3] = 0xFF;
    }
  }
}

void FlDrmCursor::RenderSnapshotRGBA(const FlCursorSnapshot& cur,
                                     uint8_t* rgba) {
  if (cur.shape == FlCursorShape::kCustom) {
    const size_t want = kCursorWidth * kCursorHeight * 4;
    if (cur.image && cur.image->size() == want) {
      memcpy(rgba, cur.image->data(), want);
    } else {
      // A custom cursor with no image behind it draws nothing, rather than
      // an arrow the guest never asked for.
      memset(rgba, 0, want);
    }
    return;
  }
  RenderShapeRGBA(cur.shape, rgba);
}

}  // namespace flutter
