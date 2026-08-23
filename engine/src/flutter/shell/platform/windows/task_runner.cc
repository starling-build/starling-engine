// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/task_runner.h"

#include <windows.h>

#include <cstdio>

#include <atomic>
#include <utility>

namespace flutter {

TaskRunner::TaskRunner(CurrentTimeProc get_current_time,
                       const TaskExpiredCallback& on_task_expired)
    : get_current_time_(get_current_time),
      on_task_expired_(std::move(on_task_expired)) {
  main_thread_id_ = GetCurrentThreadId();
  task_runner_window_ = TaskRunnerWindow::GetSharedInstance();
  task_runner_window_->AddDelegate(this);
}

TaskRunner::~TaskRunner() {
  task_runner_window_->RemoveDelegate(this);
}


namespace {
// STARLING_TRACE: name the engine tasks that cost real time during startup.
// 56 of them add up to ~69 ms on the platform thread and the framework's own
// build/layout/paint is only ~10 of that, so the rest needed a name.
bool StarlingTaskTraceOn() {
  static int enabled = -1;
  if (enabled < 0) {
    wchar_t buf[8];
    DWORD n = GetEnvironmentVariableW(L"STARLING_TRACE", buf, 8);
    enabled = (n > 0 && buf[0] != L'0') ? 1 : 0;
  }
  return enabled != 0;
}
double StarlingUptimeMs() {
  FILETIME created, exited, kernel, user, now;
  if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
    return 0.0;
  }
  GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER a, b;
  a.LowPart = created.dwLowDateTime; a.HighPart = created.dwHighDateTime;
  b.LowPart = now.dwLowDateTime;     b.HighPart = now.dwHighDateTime;
  return (double)(b.QuadPart - a.QuadPart) / 10000.0;
}
double StarlingQpcMs() {
  LARGE_INTEGER f, n;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&n);
  return (double)n.QuadPart * 1000.0 / (double)f.QuadPart;
}
}  // namespace

std::chrono::nanoseconds TaskRunner::ProcessTasks() {
  const TaskTimePoint now = GetCurrentTimeForTask();

  std::vector<Task> expired_tasks;

  // Process expired tasks.
  {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    while (!task_queue_.empty()) {
      const auto& top = task_queue_.top();
      // If this task (and all tasks after this) has not yet expired, there is
      // nothing more to do. Quit iterating.
      if (top.fire_time > now) {
        break;
      }

      // Make a record of the expired task. Do NOT service the task here
      // because we are still holding onto the task queue mutex. We don't want
      // other threads to block on posting tasks onto this thread till we are
      // done processing expired tasks.
      expired_tasks.push_back(task_queue_.top());

      // Remove the tasks from the delayed tasks queue.
      task_queue_.pop();
    }
  }

  // Fire expired tasks.
  {
    // Flushing tasks here without holing onto the task queue mutex.
    const bool trace = StarlingTaskTraceOn() && StarlingUptimeMs() < 900.0;
    for (const auto& task : expired_tasks) {
      const double t0 = trace ? StarlingQpcMs() : 0.0;
      const bool is_engine = std::get_if<FlutterTask>(&task.variant) != nullptr;
      if (auto flutter_task = std::get_if<FlutterTask>(&task.variant)) {
        on_task_expired_(flutter_task);
      } else if (auto closure = std::get_if<TaskClosure>(&task.variant))
        (*closure)();
      if (trace) {
        const double spent = StarlingQpcMs() - t0;
        if (spent >= 1.5) {
          fprintf(stderr, "[task] %6.1f ms  %s  uptime=%.1f\n", spent,
                  is_engine ? "engine(FlutterTask)" : "embedder(closure)",
                  StarlingUptimeMs());
          fflush(stderr);
        }
      }
    }
  }

  // Calculate duration to sleep for on next iteration.
  {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    const auto next_wake = task_queue_.empty() ? TaskTimePoint::max()
                                               : task_queue_.top().fire_time;

    return std::min(next_wake - now, std::chrono::nanoseconds::max());
  }
}

TaskRunner::TaskTimePoint TaskRunner::TimePointFromFlutterTime(
    uint64_t flutter_target_time_nanos) const {
  const auto now = GetCurrentTimeForTask();
  const auto flutter_duration = flutter_target_time_nanos - get_current_time_();
  return now + std::chrono::nanoseconds(flutter_duration);
}

void TaskRunner::PostFlutterTask(FlutterTask flutter_task,
                                 uint64_t flutter_target_time_nanos) {
  Task task;
  task.fire_time = TimePointFromFlutterTime(flutter_target_time_nanos);
  task.variant = flutter_task;
  EnqueueTask(std::move(task));
}

void TaskRunner::PostTask(TaskClosure closure) {
  Task task;
  task.fire_time = GetCurrentTimeForTask();
  task.variant = std::move(closure);
  EnqueueTask(std::move(task));
}

void TaskRunner::PollOnce(std::chrono::milliseconds timeout) {
  task_runner_window_->PollOnce(timeout);
}

void TaskRunner::EnqueueTask(Task task) {
  static std::atomic_uint64_t sGlobalTaskOrder(0);

  task.order = ++sGlobalTaskOrder;
  {
    std::lock_guard<std::mutex> lock(task_queue_mutex_);
    task_queue_.push(task);

    // Make sure the queue mutex is unlocked before waking up the loop. In case
    // the wake causes this thread to be descheduled for the primary thread to
    // process tasks, the acquisition of the lock on that thread while holding
    // the lock here momentarily till the end of the scope is a pessimization.
  }

  WakeUp();
}

bool TaskRunner::RunsTasksOnCurrentThread() const {
  return GetCurrentThreadId() == main_thread_id_;
}

void TaskRunner::WakeUp() {
  task_runner_window_->WakeUp();
}

}  // namespace flutter
