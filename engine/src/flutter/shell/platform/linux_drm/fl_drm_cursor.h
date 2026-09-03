// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_

#include <gbm.h>
#include <stdint.h>

#include <memory>
#include <mutex>
#include <vector>

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
  // A bitmap the caller supplied (a VM guest's own cursor). Has no number in
  // the public C enum: it is reached through fl_drm_view_set_cursor_image(),
  // never by index, and any SetShape() call replaces it.
  kCustom = 7,
};

// Cursor state at one instant, for software compositing (screen recording
// paints the cursor back into captured frames — the hardware plane it scans
// out on is invisible to GL readback). |x|,|y| are CRTC-local coordinates of
// the bitmap's top-left, hot-spot already subtracted.
struct FlCursorSnapshot {
  bool visible = false;
  uint32_t crtc_id = 0;
  int x = 0;
  int y = 0;
  FlCursorShape shape = FlCursorShape::kDefault;
  // For kCustom: the image current when the snapshot was taken, 64x64
  // straight-alpha RGBA, top-down. Immutable and shared, because a snapshot
  // outlives the instant it was taken — the recorder carries it to its
  // writer thread, where the live cursor may already be something else.
  // Null for the baked shapes, which are drawn from the enum.
  std::shared_ptr<const std::vector<uint8_t>> image;
  // Bumps on every SetImage. The overlay paths cache one rendered bitmap and
  // key it on the shape; kCustom is one shape with changing pixels, so they
  // key on this too.
  uint32_t image_gen = 0;
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

  // Put a caller-supplied bitmap on the plane. |bgra| is |width|x|height|
  // straight-alpha BGRA8888, tightly packed, clipped to the 64x64 plane;
  // the KMS blend is pre-multiplied, so this pre-multiplies on copy. The
  // hot-spot is in image pixels. A width or height of 0 hides the sprite by
  // uploading a transparent image. Unlike SetShape there is no early-out:
  // two successive images with the same dimensions are two different
  // pictures.
  void SetImage(const uint8_t* bgra, int width, int height, int hot_x,
                int hot_y);

  // Consistent copy of the current cursor state. Safe from any thread —
  // position moves on the platform thread, shape on the UI thread, and the
  // recorder reads from its writer thread.
  FlCursorSnapshot Snapshot() const;

  // Render |shape|'s bitmap as straight RGBA (64×64×4, top-down, alpha 0
  // where the cursor buffer is transparent) for software compositing.
  static void RenderShapeRGBA(FlCursorShape shape, uint8_t* rgba);

  // The same, for whatever |cur| was showing — a baked shape or the custom
  // image it carries. This is what the capture paths want: they hold a
  // snapshot, not the live cursor.
  static void RenderSnapshotRGBA(const FlCursorSnapshot& cur, uint8_t* rgba);

 private:
  // Write the bitmap for |shape| into cursor_bo_.
  void LoadShape(FlCursorShape shape);

  int drm_fd_ = -1;
  uint32_t crtc_id_ = 0;
  gbm_bo* cursor_bo_ = nullptr;
  bool visible_ = false;
  FlCursorShape current_shape_ = FlCursorShape::kDefault;

  // Guards the snapshot fields (crtc_id_, visible_, current_shape_, hot spot,
  // last position) — written from the platform and UI threads, read by
  // Snapshot() from the recorder's writer thread.
  mutable std::mutex state_mu_;

  // Hot-spot of the current shape (offset of the "click point" inside the
  // bitmap). The legacy drmModeSetCursor API has no hot-spot support, so
  // MoveTo subtracts it from the pointer position instead.
  int hot_x_ = 0;
  int hot_y_ = 0;
  // Last pointer position, so a shape change re-anchors the plane with the
  // new hot-spot without waiting for the next motion event.
  int last_x_ = 0;
  int last_y_ = 0;

  // The current custom image as straight RGBA, handed out by Snapshot().
  // Replaced wholesale on every SetImage, never mutated in place, so a
  // snapshot taken earlier keeps the picture it saw.
  std::shared_ptr<const std::vector<uint8_t>> custom_rgba_;
  uint32_t image_gen_ = 0;

  static constexpr uint32_t kCursorWidth = 64;
  static constexpr uint32_t kCursorHeight = 64;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_CURSOR_H_
