// STARLING: which Flutter view the raster thread is drawing right now.
//
// The rasterizer draws every view into one Surface, and Surface::AcquireFrame
// carries no view id, so a surface that owns one drawable per view (the iOS
// Metal surface: one CAMetalLayer per view) cannot tell which one a frame is
// for. Rasterizer::DrawToSurfaceUnsafe sets this around each view's draw; the
// surface reads it while acquiring the frame (same thread).

#ifndef FLUTTER_SHELL_COMMON_RASTER_VIEW_ID_H_
#define FLUTTER_SHELL_COMMON_RASTER_VIEW_ID_H_

#include <cstdint>

namespace flutter {

inline int64_t& CurrentRasterViewId() {
  static thread_local int64_t id = 0;  // kFlutterImplicitViewId
  return id;
}

}  // namespace flutter

#endif  // FLUTTER_SHELL_COMMON_RASTER_VIEW_ID_H_
