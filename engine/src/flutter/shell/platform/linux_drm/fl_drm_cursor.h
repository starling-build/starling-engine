// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_

#include <gbm.h>
#include <stdint.h>

namespace flutter {

// Cursor shapes the shell can switch between. Keep in sync with the
// public C API enum values declared in fl_drm_view.h.
enum class FlCursorShape : int {
  kDefault = 0,    // Arrow (top-left point).
  kResizeNS = 1,   // Vertical double arrow (top/bottom edges).
  kResizeEW = 2,   // Horizontal double arrow (left/right edges).
  kResizeNESW = 3, // Diagonal: top-right / bottom-left corners.
  kResizeNWSE = 4, // Diagonal: top-left  / bottom-right corners.
  kText = 5,       // I-beam (over editable text).
  kPointer = 6,    // Pointing hand (over links).
};

class FlDrmCursor {
 public:
  FlDrmCursor();
  ~FlDrmCursor();

  // Initialize hardware cursor on the given CRTC.
  // Creates a GBM buffer object for the cursor image.
  bool Initialize(int drm_fd, uint32_t crtc_id, gbm_device* gbm_device);

  // Move the hardware cursor to (x, y) on the current CRTC.
  void MoveTo(int x, int y);

  // Move the cursor to CRTC-local (x, y), migrating the cursor plane when
  // `crtc_id` differs from the current CRTC (pointer crossed outputs).
  void MoveTo(uint32_t crtc_id, int x, int y);

  // Show/hide the cursor.
  void SetVisible(bool visible);

  // Restore cursor after VT switch (re-apply to DRM hardware plane).
  void Restore();

  // Swap the cursor bitmap to the given shape. Cheap when the shape is
  // already current — only rewrites the GBM buffer on change.
  void SetShape(FlCursorShape shape);

 private:
  // Write the bitmap for |shape| into cursor_bo_.
  void LoadShape(FlCursorShape shape);

  int drm_fd_ = -1;
  uint32_t crtc_id_ = 0;
  gbm_bo* cursor_bo_ = nullptr;
  bool visible_ = false;
  FlCursorShape current_shape_ = FlCursorShape::kDefault;

  // Hot-spot of the current shape (offset of the "click point" inside the
  // bitmap). The legacy drmModeSetCursor API has no hot-spot support, so
  // MoveTo subtracts it from the pointer position instead.
  int hot_x_ = 0;
  int hot_y_ = 0;
  // Last pointer position, so a shape change re-anchors the plane with the
  // new hot-spot without waiting for the next motion event.
  int last_x_ = 0;
  int last_y_ = 0;

  static constexpr uint32_t kCursorWidth = 64;
  static constexpr uint32_t kCursorHeight = 64;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_
