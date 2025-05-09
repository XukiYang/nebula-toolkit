/*
 * @Descripttion:
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-05-09 11:59:16
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-05-09 16:55:02
 */
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using CallBack = std::function<size_t(void)>;

class ThreadPool {
public:
  /// @brief 指定线程数构造线程
  /// @param thread_count
  explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency())
      : running_(true) {
    for (size_t i = 0; i < thread_count; ++i) {
      workers_.emplace_back(&ThreadPool::WorkerLoop, this);
    }
  }

  /// @brief 提交回调任务
  /// @param cb_task
  void PostTask(CallBack cb_task) {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      task_queue_.push(std::move(cb_task));
    }
    cv_.notify_one(); // 唤醒一个线程
  }

  /// @brief 批量提交回调任务，适用于高频小任务提交
  /// @param cb_tasks
  void PostTask(std::vector<CallBack> &&cb_tasks) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (const auto &cb_task : cb_tasks) {
      task_queue_.push(std::move(cb_task));
    }
    cv_.notify_all(); // 唤醒所有线程
  }

  /// @brief 析构线程池
  ~ThreadPool() {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      running_.store(false, std::memory_order_release);
    }
    cv_.notify_all();
    for (auto &worker : workers_) {
      if (worker.joinable())
        worker.join();
    }
  }

private:
  /// @brief 被动任务消费循环
  /// @note 根据条件变量决定是否从任务队列中取出任务并执行
  void WorkerLoop() {
    while (true) {
      CallBack cb_task;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        cv_.wait(lock, [this] {
          return !running_.load(std::memory_order_acquire) ||
                 !task_queue_.empty();
        });

        if (!running_ && task_queue_.empty())
          return;

        cb_task = std::move(task_queue_.front());
        task_queue_.pop();
      }
      cb_task();
    }
  }

  std::vector<std::thread> workers_;
  std::queue<CallBack> task_queue_; // 任务队列（FIFO）

  std::mutex queue_mutex_;
  std::condition_variable cv_;
  std::atomic<bool> running_;
};