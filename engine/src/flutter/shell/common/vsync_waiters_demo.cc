// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Minimal implementation of vsync_waiters_test.h for the blue_screen_demo.
// The original vsync_waiters_test.cc includes heavy headers (shell_test.h,
// dart_vm.h, testing.h) that are not actually used by the implementations
// but cause GN dependency issues when compiled outside shell_test_fixture_sources.

#define FML_USED_ON_EMBEDDER

#include "flutter/shell/common/vsync_waiters_test.h"

#include <future>

namespace flutter {
namespace testing {

void ShellTestVsyncClock::SimulateVSync() {
  std::scoped_lock lock(mutex_);
  if (vsync_issued_ >= vsync_promised_.size()) {
    vsync_promised_.emplace_back();
  }
  FML_CHECK(vsync_issued_ < vsync_promised_.size());
  vsync_promised_[vsync_issued_].set_value(vsync_issued_);
  vsync_issued_ += 1;
}

std::future<int> ShellTestVsyncClock::NextVSync() {
  std::scoped_lock lock(mutex_);
  vsync_promised_.emplace_back();
  return vsync_promised_.back().get_future();
}

void ShellTestVsyncWaiter::AwaitVSync() {
  FML_DCHECK(task_runners_.GetUITaskRunner()->RunsTasksOnCurrentThread());
  auto vsync_future = clock_->NextVSync();

  auto async_wait = std::async([&vsync_future, this]() {
    vsync_future.wait();
    task_runners_.GetPlatformTaskRunner()->PostTask([this]() {
      FireCallback(fml::TimePoint::Now(), fml::TimePoint::Now());
    });
  });
}

void ConstantFiringVsyncWaiter::AwaitVSync() {
  FML_DCHECK(task_runners_.GetUITaskRunner()->RunsTasksOnCurrentThread());
  auto async_wait = std::async([this]() {
    task_runners_.GetPlatformTaskRunner()->PostTask(
        [this]() { FireCallback(kFrameBeginTime, kFrameTargetTime); });
  });
}

TestRefreshRateReporter::TestRefreshRateReporter(double refresh_rate)
    : refresh_rate_(refresh_rate) {}

void TestRefreshRateReporter::UpdateRefreshRate(double refresh_rate) {
  refresh_rate_ = refresh_rate;
}

double TestRefreshRateReporter::GetRefreshRate() const {
  return refresh_rate_;
}

}  // namespace testing
}  // namespace flutter
