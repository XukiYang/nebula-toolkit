#pragma once
#include "thread_pool.hpp"
#include <unordered_set>

struct TimerTask {
  std::chrono::steady_clock::time_point exec_time;
  CallBack callback;
  uint64_t task_id; // 用于任务取消

  bool operator<(const TimerTask &other) const {
    return exec_time > other.exec_time; // 小堆序
  }
};

class TimerScheduler {
private:
  std::priority_queue<TimerTask> timer_tasks_;    // 优先级队列(小根堆)
  std::unordered_set<uint64_t> canceled_ids_;     // 退出任务ID set
  std::unique_ptr<std::thread> scheduler_thread_; // 调度器线程
  std::condition_variable scheduler_cv_;          // 调度器线程条件变量
  std::unique_ptr<ThreadPool> thread_pool_;       // 线程池
  std::atomic<uint64_t> next_id_{0};              // 任务ID
  std::mutex mutex_;                              // 保护优先级队列锁
  std::atomic<bool> running_{false};              // 调度器线程执行原子

public:
  explicit TimerScheduler(
      size_t thread_count = std::thread::hardware_concurrency())
      : thread_pool_(std::make_unique<ThreadPool>(thread_count)){

        };
  ~TimerScheduler() {
    Stop();
    if (scheduler_thread_ && scheduler_thread_->joinable()) {
      scheduler_thread_->join();
    }
  };

public:
  /// @brief 提交定时任务
  /// @param delay_ms
  /// @param cb_task
  /// @return 任务ID用于定时任务取消
  uint64_t ScheduleOnce(uint64_t delay_ms, CallBack cb_task) {
    uint64_t id = next_id_.fetch_add(1);
    auto now = std::chrono::steady_clock::now();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      timer_tasks_.push(
          {now + std::chrono::milliseconds(delay_ms), std::move(cb_task), id});
    }
    scheduler_cv_.notify_one();
    return id;
  };

  /// @brief 定时任务取消
  /// @param task_id
  /// @return
  bool Cancel(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    canceled_ids_.insert(task_id);
    return true;
  }

  /// @brief 启动调度器
  void Start() {
    if (!running_.load()) {
      running_.store(true);
      scheduler_thread_ =
          std::make_unique<std::thread>(&TimerScheduler::RunScheduler, this);
    }
  };

  /// @brief 停止调调度器
  void Stop() {
    if (running_) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
      }
      // 唤醒线程退出
      scheduler_cv_.notify_all();
      if (scheduler_thread_->joinable()) {
        scheduler_thread_->join();
      }
    }
  };

  /// @brief 调度器主循环
  void RunScheduler() {
    while (running_.load()) {
      std::unique_lock<std::mutex> lock(mutex_);
      // 若无定时任务，则主循环线程休息
      if (timer_tasks_.empty()) {
        scheduler_cv_.wait(
            lock, [this] { return !running_ || !timer_tasks_.empty(); });
        continue;
      }
      // 判断是否为取消任务ID
      auto task_id = timer_tasks_.top().task_id;
      if (canceled_ids_.count(task_id)) {
        timer_tasks_.pop();           // 出队
        canceled_ids_.erase(task_id); // 删除
        continue;
      }

      const auto &next_timer_task = timer_tasks_.top();
      // 到时间的任务则提交到线程池托管执行
      if (std::chrono::steady_clock::now() >= next_timer_task.exec_time) {
        auto cur_timer_task = next_timer_task;
        timer_tasks_.pop(); // 出队
        lock.unlock();      // 释放锁后再提交任务
        thread_pool_->PostTask(std::move(cur_timer_task.callback));
      } else {
        scheduler_cv_.wait_until(lock, next_timer_task.exec_time);
      }
    }
  };
};