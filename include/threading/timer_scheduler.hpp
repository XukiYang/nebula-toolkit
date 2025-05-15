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
  std::priority_queue<TimerTask> tasks_; // 优先级队列(小根堆)
  std::unordered_set<uint64_t> canceled_ids_;
  std::unique_ptr<std::thread *> scheduler_thread_; // 调度器线程
  std::unique_ptr<ThreadPool> tp_;                  // 线程池
  std::atomic<uint64_t> next_id_;                   // 任务ID
  std::mutex mutex_;                                // 保护优先级队列锁
  std::atomic<bool> running_{false}; // 调度器线程执行原子

public:
  TimerScheduler(size_t thread_count = std::thread::hardware_concurrency())
      : tp_(std::make_unique<ThreadPool>(thread_count)){

        };
  ~TimerScheduler(){};

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
      tasks_.push(
          {now + std::chrono::milliseconds(delay_ms), std::move(cb_task), id});
    }
    return id;
  };

  /// @brief 定时任务取消
  /// @param task_id
  /// @return
  bool Cancel(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    canceled_ids_.insert(task_id);
  }

  /// @brief 启动调度器
  void Start() {
    if (!running_) {
      running_ = true;
      scheduler_thread_ =
          std::make_unique<std::thread *>(&TimerScheduler::RunScheduler, this);
    }
  };

  /// @brief 停止调调度器
  void Stop(){

  };

  /// @brief 调度器主循环
  void RunScheduler(){

  };
};