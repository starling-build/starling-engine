// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Public C API for the DRM/KMS Flutter shell.
// This is the ONLY header Swift needs to import.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_VIEW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_VIEW_H_

#include <stdint.h>

#ifdef FLUTTER_LINUX_DRM_COMPILATION
#define FL_DRM_EXPORT __attribute__((visibility("default")))
#else
#define FL_DRM_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlDrmView FlDrmView;

// Opaque Flutter engine handle (matches embedder.h typedef).
typedef void* FlDrmFlutterEngine;

// Create a DRM view. Opens /dev/dri/card0 (or FLUTTER_DRM_DEVICE env),
// sets up GBM+EGL, starts libinput, initializes Flutter engine.
//
// |assets_path|        Path to flutter_assets directory.
// |icu_data_path|      Path to icudtl.dat.
// |runtime_controller|  Opaque RuntimeControllerInterface* for Swift mode.
//                       Pass NULL for standard Dart mode.
FL_DRM_EXPORT FlDrmView* fl_drm_view_create(const char* assets_path,
                                             const char* icu_data_path,
                                             void* runtime_controller);

// Run the event loop (blocks). Handles DRM page flips, libinput events,
// engine task scheduling. Returns when fl_drm_view_shutdown() is called
// or a fatal error occurs.
FL_DRM_EXPORT void fl_drm_view_run(FlDrmView* view);

// Request shutdown (safe to call from signal handler).
FL_DRM_EXPORT void fl_drm_view_shutdown(FlDrmView* view);

// Get display dimensions (pixels).
FL_DRM_EXPORT uint32_t fl_drm_view_get_width(FlDrmView* view);
FL_DRM_EXPORT uint32_t fl_drm_view_get_height(FlDrmView* view);

// Get the Flutter engine handle (for sending events, etc.)
FL_DRM_EXPORT FlDrmFlutterEngine fl_drm_view_get_engine(FlDrmView* view);

// Get an OpenGL/EGL function address (replaces glfwGetProcAddress for DRM mode).
FL_DRM_EXPORT void* fl_drm_view_get_proc_address(const char* name);

// Callback type for external texture population.
// Parameters: user_data, texture_id, width, height, texture_out.
// Returns true if the texture was populated.
typedef int (*FlDrmExternalTextureCallback)(void* user_data,
                                            int64_t texture_id,
                                            int width,
                                            int height,
                                            void* texture_out);

// Register an external texture frame callback. The DRM engine will call
// this when a registered external texture needs to be populated.
FL_DRM_EXPORT void fl_drm_view_set_external_texture_callback(
    FlDrmView* view,
    FlDrmExternalTextureCallback callback,
    void* user_data);

// Get the EGL display (for DMA-BUF import via eglCreateImageKHR).
FL_DRM_EXPORT void* fl_drm_view_get_egl_display(FlDrmView* view);

// Register an external fd to be polled in the DRM event loop.
// When the fd is readable, |callback| is called and a frame is scheduled.
// Call BEFORE fl_drm_view_run().
FL_DRM_EXPORT void fl_drm_view_add_external_fd(FlDrmView* view,
                                                int fd,
                                                void (*callback)(void* user_data),
                                                void* user_data);

// Re-send window metrics with a new pixel ratio (for runtime DPI changes).
FL_DRM_EXPORT void fl_drm_view_send_metrics(FlDrmView* view,
                                             double pixel_ratio);

// Request a screenshot on the next frame. Sets the internal flag that
// causes glReadPixels to dump a PPM file in the present callback.
// Safe to call from any thread (uses atomic flag).
FL_DRM_EXPORT void fl_drm_view_request_screenshot(void);

// X11 screen capture (for the in-tree X server's GetImage → Zoom screen share).
// arm_capture: mark that a client wants pixels; captures the presented desktop
// into a CPU mirror for the next couple of frames and schedules a frame so an
// idle desktop still refreshes. Safe from any thread. read_capture: copy the
// screen rect [x,y,w,h] into dst as X ZPixmap depth-32 BGRX, top-down; returns
// 1 on success, 0 if no frame has been mirrored yet. dst_len must be >= w*h*4.
FL_DRM_EXPORT void fl_drm_view_arm_capture(FlDrmView* view);
FL_DRM_EXPORT int fl_drm_view_read_capture(int x, int y, int w, int h,
                                           uint8_t* dst, int dst_len);
// Non-zero while a GetImage client is actively capturing — the shell's
// frame-tick pump forces presents while this holds so the mirror refreshes.
FL_DRM_EXPORT int fl_drm_view_capture_active(void);

// Cursor shapes — must stay in sync with flutter::CursorShape.
typedef enum {
  FL_DRM_CURSOR_DEFAULT = 0,
  FL_DRM_CURSOR_RESIZE_NS = 1,
  FL_DRM_CURSOR_RESIZE_EW = 2,
  FL_DRM_CURSOR_RESIZE_NESW = 3,
  FL_DRM_CURSOR_RESIZE_NWSE = 4,
  FL_DRM_CURSOR_TEXT = 5,
  FL_DRM_CURSOR_POINTER = 6,
} FlDrmCursorShape;

// Swap the hardware cursor bitmap to the requested shape. No-op if the
// shape is already current. Safe to call from the UI thread.
FL_DRM_EXPORT void fl_drm_view_set_cursor_shape(FlDrmView* view,
                                                 int shape);

// Present (page-flip) notification — fired on the PLATFORM thread each time
// a queued flip lands on the display, with the kernel's scanout timestamp
// (CLOCK_MONOTONIC ns) and the display's refresh period in ns. Use this to
// pace Wayland frame callbacks / presentation feedback off real vsync.
typedef void (*FlDrmPresentCallback)(void* user_data,
                                     uint64_t flip_time_ns,
                                     uint32_t refresh_ns);
FL_DRM_EXPORT void fl_drm_view_set_present_callback(FlDrmView* view,
                                                     FlDrmPresentCallback cb,
                                                     void* user_data);

// Display refresh rate in mHz (e.g. 30000 for a 30 Hz panel), from the
// active DRM mode. For wl_output.mode advertisement.
FL_DRM_EXPORT uint32_t fl_drm_view_get_refresh_mhz(FlDrmView* view);

// ─── Multi-output ────────────────────────────────────────────────────────────
// Number of connected outputs (each with its own swap chain). The implicit
// Flutter view (id 0) always renders to the primary output.
FL_DRM_EXPORT uint32_t fl_drm_view_get_output_count(FlDrmView* view);

// Info for output `index` (0..count-1). Out-pointers may be NULL. `name_buf`
// receives the connector name ("HDMI-A-1") truncated to name_buf_size.
// Returns 1 on success, 0 on a bad index.
FL_DRM_EXPORT int fl_drm_view_get_output_info(FlDrmView* view,
                                               uint32_t index,
                                               uint32_t* width,
                                               uint32_t* height,
                                               uint32_t* refresh_mhz,
                                               int* is_primary,
                                               char* name_buf,
                                               uint32_t name_buf_size);

// Create Flutter view `flutter_view_id` (unique, nonzero) rendering to output
// `index`, with the given pixel ratio. The app decides which outputs get
// views and what they show (via its multi-view content builder). Requires the
// compositor path (default). Returns 1 on success, 0 on failure.
FL_DRM_EXPORT int fl_drm_view_add_output_view(FlDrmView* view,
                                               uint32_t index,
                                               int64_t flutter_view_id,
                                               double pixel_ratio);

// Place output `index` at (logical_x, logical_y) with `scale` in the global
// logical ("virtual desktop") pointer space. The pointer moves across placed
// outputs; events carry the owning Flutter view's id and view-local physical
// coordinates, and the hardware cursor follows onto that output's CRTC.
// Call for every output after adding its view (the primary defaults to
// (0,0) at FLUTTER_DRM_DPI). Outputs without a placement or view don't
// participate in pointer motion.
FL_DRM_EXPORT void fl_drm_view_set_output_layout(FlDrmView* view,
                                                  uint32_t index,
                                                  double logical_x,
                                                  double logical_y,
                                                  double scale);

// Invoked when the connected-output set changes (monitor hotplug): the
// engine has already enumerated the new output(s) and modeset them black.
// Runs on the ENGINE PLATFORM thread — deliberately, because that is the
// thread fl_drm_view_add_output_view (FlutterEngineAddView) must be called
// from once the engine is running. Query outputs, add views, and set the
// layout directly in the callback; marshal UI work to your main thread.
typedef void (*FlDrmOutputsChangedCallback)(void* user_data);
FL_DRM_EXPORT void fl_drm_view_set_outputs_changed_callback(
    FlDrmView* view,
    FlDrmOutputsChangedCallback callback,
    void* user_data);

// Destroy and free all resources. Restores original CRTC, closes DRM fd.
FL_DRM_EXPORT void fl_drm_view_destroy(FlDrmView* view);

#ifdef __cplusplus
}
#endif

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_VIEW_H_
