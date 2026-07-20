// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_TASK_RUNNER_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_TASK_RUNNER_H_

#include <pthread.h>
#include <stdint.h>

#include <mutex>
#include <queue>
#include <vector>

#include "embedder.h"

namespace flutter {

class FlDrmTaskRunner {
 public:
  FlDrmTaskRunner();
  ~FlDrmTaskRunner();

  // Get the timerfd for epoll integration.
  int timer_fd() const { return timer_fd_; }

  // Post a task to be run at the given time (nanoseconds, engine clock).
  void PostTask(FlutterTask task, uint64_t target_time_nanos);

  // Drain all expired tasks and run them via FlutterEngineRunTask.
  // Returns the next deadline in nanoseconds, or 0 if no pending tasks.
  uint64_t DrainExpired(FlutterEngine engine);

  // Check if the calling thread is the platform thread.
  bool RunsOnCurrentThread() const;

  // Record the platform thread id (call from main thread).
  void SetPlatformThread();

 private:
  struct PendingTask {
    FlutterTask task;
    uint64_t target_time_nanos;
    bool operator>(const PendingTask& other) const {
      return target_time_nanos > other.target_time_nanos;
    }
  };

  void UpdateTimerLocked();

  int timer_fd_ = -1;
  pthread_t platform_thread_ = 0;
  std::mutex mutex_;
  std::priority_queue<PendingTask, std::vector<PendingTask>,
                      std::greater<PendingTask>> tasks_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_DRM_FL_DRM_TASK_RUNNER_H_
