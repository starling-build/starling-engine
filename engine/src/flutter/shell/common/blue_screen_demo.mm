// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Blue Screen Demo — validates the full Dart-free rendering pipeline:
//   C++ Shell → SwiftRuntimeController (C callbacks) → SwiftRuntimeDelegate
//   → PlatformDispatcher.onBeginFrame → Swift builds blue Scene
//   → FlutterView.render(scene) → ViewBridge::RenderView
//   → Engine → Rasterizer → offscreen Metal texture

// The Flutter bundled Clang supports __has_attribute(swift_attr) but places
// the attribute in contexts that cause parse errors. Include <swift/bridging>
// first, then redefine SWIFT_NONCOPYABLE to empty.
#include <swift/bridging>
#undef SWIFT_NONCOPYABLE
#define SWIFT_NONCOPYABLE

// Must be included before the generated Swift header, which uses
// SwiftRuntimeCallbacks by value but doesn't define it.
#include "flutter/lib/ui/swift/include/swift_runtime_callbacks.h"

#include <SwiftRuntime-Swift.h>

#include <chrono>
#include <thread>

#include "flutter/common/constants.h"
#include "flutter/fml/synchronization/waitable_event.h"
#include "flutter/lib/ui/swift/include/swift_bridge_engine_registry.h"
#include "flutter/lib/ui/swift/include/swift_runtime_controller.h"
#include "flutter/shell/common/rasterizer.h"
#include "flutter/shell/common/shell.h"
#include "flutter/shell/common/shell_test_platform_view.h"
#include "flutter/shell/common/thread_host.h"
#include "flutter/shell/common/vsync_waiters_test.h"

int main(int argc, char* argv[]) {
  // 1. Create ThreadHost with 4 threads.
  flutter::ThreadHost thread_host(
      "blue_screen_demo.",
      flutter::ThreadHost::Type::kPlatform |
          flutter::ThreadHost::Type::kUi |
          flutter::ThreadHost::Type::kRaster |
          flutter::ThreadHost::Type::kIo);

  flutter::TaskRunners task_runners(
      "blue_screen_demo",
      thread_host.platform_thread->GetTaskRunner(),
      thread_host.raster_thread->GetTaskRunner(),
      thread_host.ui_thread->GetTaskRunner(),
      thread_host.io_thread->GetTaskRunner());

  // 2. Create SwiftRuntimeController from the C callback table.
  //    The Swift side fills in function pointers backed by SwiftRuntimeDelegate.
  auto callbacks = SwiftRuntime::createRuntimeCallbacks();
  auto runtime_controller =
      std::make_unique<flutter::SwiftRuntimeController>(callbacks);

  // 3. Call Swift setup (sets PlatformDispatcher.onBeginFrame + onDrawFrame).
  SwiftRuntime::setupBlueScreenDemo();

  // 4. Create VSync clock for frame triggering.
  auto vsync_clock =
      std::make_shared<flutter::testing::ShellTestVsyncClock>();

  // 5. Create platform view callback (Metal offscreen).
  auto on_create_platform_view = [&vsync_clock](flutter::Shell& shell) {
    auto create_vsync_waiter = [&vsync_clock,
                                &task_runners = shell.GetTaskRunners()]() {
      return std::make_unique<flutter::testing::ShellTestVsyncWaiter>(
          task_runners, vsync_clock);
    };
    return flutter::testing::ShellTestPlatformView::Create(
        flutter::testing::ShellTestPlatformView::BackendType::kMetalBackend,
        shell,                              // PlatformView::Delegate&
        shell.GetTaskRunners(),             // task_runners
        vsync_clock,                        // vsync_clock
        create_vsync_waiter,                // create_vsync_waiter
        nullptr,                            // external_view_embedder
        shell.GetIsGpuDisabledSyncSwitch()  // gpu_disabled_switch
    );
  };

  auto on_create_rasterizer = [](flutter::Shell& shell) {
    return std::make_unique<flutter::Rasterizer>(shell);
  };

  // 6. Create Shell (no Dart VM).
  flutter::Settings settings;
  settings.task_observer_add = [](intptr_t, const fml::closure&) {
    return fml::TaskQueueId(0);
  };
  settings.task_observer_remove = [](fml::TaskQueueId, intptr_t) {};

  auto shell = flutter::Shell::CreateSwift(
      flutter::PlatformData{},     //
      task_runners,                //
      settings,                    //
      on_create_platform_view,     //
      on_create_rasterizer,        //
      std::move(runtime_controller));

  if (!shell) {
    fprintf(stderr, "[blue_screen_demo] FAILED: Could not create shell\n");
    return 1;
  }

  // 7. Reset frame rendered flag.
  flutter::swift_bridge::SwiftBridgeEngineRegistry::ResetFrameRendered();

  // 8. Set viewport metrics on the implicit view (view 0, already added by
  //    Shell::Setup). Must be called on the platform thread.
  flutter::swift_bridge::SwiftBridgeEngineRegistry::SetDevicePixelRatio(2.0);

  fml::AutoResetWaitableEvent metrics_latch;
  fml::TaskRunner::RunNowOrPostTask(
      task_runners.GetPlatformTaskRunner(), [&shell, &metrics_latch]() {
        flutter::ViewportMetrics metrics;
        metrics.device_pixel_ratio = 2.0;
        metrics.physical_width = 800;
        metrics.physical_height = 600;
        shell->GetPlatformView()->SetViewportMetrics(
            flutter::kFlutterImplicitViewId, metrics);
        metrics_latch.Signal();
      });
  metrics_latch.Wait();

  // 9. Schedule frame from Swift → Engine → Animator → VsyncWaiter.
  //    Must be posted to the UI thread because Engine::ScheduleFrame() has
  //    thread affinity (WeakPtrFactory created on UI thread).
  fml::AutoResetWaitableEvent schedule_latch;
  fml::TaskRunner::RunNowOrPostTask(
      task_runners.GetUITaskRunner(), [&schedule_latch]() {
        SwiftRuntime::scheduleFirstFrame();
        schedule_latch.Signal();
      });
  schedule_latch.Wait();

  // 10. Fire simulated VSync.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  vsync_clock->SimulateVSync();

  // 11. Wait for frame to complete.
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 12. Verify frame was rendered.
  bool rendered =
      flutter::swift_bridge::SwiftBridgeEngineRegistry::FrameWasRendered();

  if (rendered) {
    printf("[blue_screen_demo] Frame rendered successfully!\n");
  } else {
    printf("[blue_screen_demo] WARNING: Frame rendered flag not set.\n");
  }

  printf("[blue_screen_demo] Pipeline: Shell::CreateSwift"
         " -> SwiftRuntimeController (C callbacks)\n");
  printf("[blue_screen_demo]   -> SwiftRuntimeDelegate"
         " -> PlatformDispatcher.onBeginFrame\n");
  printf("[blue_screen_demo]   -> Swift builds blue scene"
         " -> FlutterView.render(scene)\n");
  printf("[blue_screen_demo]   -> ViewBridge::RenderView"
         " -> Engine -> Rasterizer -> Metal\n");
  printf("[blue_screen_demo] %s\n", rendered ? "PASSED" : "FAILED");

  // 13. Cleanup — must destroy Shell on the platform thread since its
  //     WeakPtrFactory was created there.
  fml::AutoResetWaitableEvent shutdown_latch;
  fml::TaskRunner::RunNowOrPostTask(
      task_runners.GetPlatformTaskRunner(),
      [&shell, &shutdown_latch]() {
        shell.reset();
        shutdown_latch.Signal();
      });
  shutdown_latch.Wait();

  return rendered ? 0 : 1;
}
