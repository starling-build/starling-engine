// Copyright the Starling authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_drm_task_runner.h"

#include <stdio.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace flutter {

FlDrmTaskRunner::FlDrmTaskRunner() {
  timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (timer_fd_ < 0) {
    fprintf(stderr, "[TaskRunner] timerfd_create failed\n");
  }
  platform_thread_ = pthread_self();
}

FlDrmTaskRunner::~FlDrmTaskRunner() {
  if (timer_fd_ >= 0) {
    close(timer_fd_);
  }
}

void FlDrmTaskRunner::SetPlatformThread() {
  platform_thread_ = pthread_self();
}

bool FlDrmTaskRunner::RunsOnCurrentThread() const {
  return pthread_equal(pthread_self(), platform_thread_) != 0;
}

void FlDrmTaskRunner::PostTask(FlutterTask task,
                                uint64_t target_time_nanos) {
  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.push({task, target_time_nanos});
  UpdateTimerLocked();
}

uint64_t FlDrmTaskRunner::DrainExpired(FlutterEngine engine) {
  // Read the timerfd to clear it.
  uint64_t expirations;
  if (timer_fd_ >= 0) {
    read(timer_fd_, &expirations, sizeof(expirations));
  }

  uint64_t now = FlutterEngineGetCurrentTime();

  std::vector<FlutterTask> expired;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!tasks_.empty() && tasks_.top().target_time_nanos <= now) {
      expired.push_back(tasks_.top().task);
      tasks_.pop();
    }
  }

  for (auto& task : expired) {
    FlutterEngineRunTask(engine, &task);
  }

  // Return the next deadline.
  std::lock_guard<std::mutex> lock(mutex_);
  if (!tasks_.empty()) {
    UpdateTimerLocked();
    return tasks_.top().target_time_nanos;
  }
  return 0;
}

void FlDrmTaskRunner::UpdateTimerLocked() {
  if (timer_fd_ < 0 || tasks_.empty()) {
    return;
  }

  // Get current time from the same clock as the engine (CLOCK_MONOTONIC).
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  uint64_t now_nanos = static_cast<uint64_t>(now.tv_sec) * 1000000000ULL +
                        static_cast<uint64_t>(now.tv_nsec);

  uint64_t target = tasks_.top().target_time_nanos;
  uint64_t delay_nanos = (target > now_nanos) ? (target - now_nanos) : 1;

  struct itimerspec spec = {};
  spec.it_value.tv_sec = static_cast<time_t>(delay_nanos / 1000000000ULL);
  spec.it_value.tv_nsec = static_cast<long>(delay_nanos % 1000000000ULL);

  timerfd_settime(timer_fd_, 0, &spec, nullptr);
}

}  // namespace flutter
