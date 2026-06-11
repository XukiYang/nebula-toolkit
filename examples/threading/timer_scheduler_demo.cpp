// =============================================================================
// timer_scheduler_demo.cpp -- TimerScheduler 定时调度器教学示例
// =============================================================================
//
// 核心思想:
//   TimerScheduler 使用小根堆 (min-heap) + 条件变量实现定时调度:
//   1. 小根堆按执行时间排序，堆顶始终是最近要执行的任务
//   2. 调度线程等待条件变量，超时后取出到期任务
//   3. 到期任务分发给内联线程池执行，不阻塞调度线程
//
// TimerScheduler 的关键设计:
//   - 内联线程池: 构造时指定工作线程数，任务回调在工作线程中执行
//   - 任务取消: 通过 task_id 标记取消，到期时跳过已取消的任务
//   - 线程安全: 多线程可同时提交 ScheduleOnce
//
// =============================================================================

#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <threading/timer_scheduler.hpp>

int main() {
    using nebula::threading::TimerScheduler;

    fmt::println("========================================");
    fmt::println("  TimerScheduler 定时调度器教学示例");
    fmt::println("========================================\n");

    // 1. 延时任务 ScheduleOnce
    //    ScheduleOnce(delay_ms, callback) 注册一个延时任务
    //    callback 返回值目前未使用，约定返回 0
    //    返回 task_id 用于后续取消
    fmt::println("--- 1. 延时任务 ScheduleOnce ---");
    {
        TimerScheduler scheduler(2);  // 2 个工作线程
        scheduler.Start();

        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;

        auto start = std::chrono::steady_clock::now();

        scheduler.ScheduleOnce(100, [&]() -> size_t {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            fmt::println("  任务执行! 延时约 100ms, 实际 {}ms", elapsed);
            {
                std::lock_guard<std::mutex> lk(mtx);
                done = true;
            }
            cv.notify_one();
            return 0;
        });

        // 等待任务完成
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(1), [&] { return done; });
        }

        scheduler.Stop();
    }

    // 2. 任务取消 Cancel
    //    ScheduleOnce 返回 task_id，Cancel(task_id) 标记取消
    //    已取消的任务在到期时会被跳过
    fmt::println("\n--- 2. 任务取消 Cancel ---");
    {
        TimerScheduler scheduler(2);
        scheduler.Start();

        std::atomic<bool> executed{false};

        // 注册一个 500ms 后执行的任务
        uint64_t task_id = scheduler.ScheduleOnce(500, [&]() -> size_t {
            executed.store(true);
            fmt::println("  [不应出现] 此任务已被取消");
            return 0;
        });

        fmt::println("  注册任务 id={}, 延时 500ms", task_id);

        // 立即取消
        bool cancelled = scheduler.Cancel(task_id);
        fmt::println("  取消任务: {}", cancelled ? "成功" : "失败");

        // 等待足够时间，确认任务未执行
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        fmt::println("  任务是否执行: {}", executed.load() ? "是" : "否");

        scheduler.Stop();
    }

    // 3. 多任务并发执行
    //    多个任务注册到调度器，到期后由线程池并发执行
    //    任务的实际执行顺序取决于线程池调度，可能与到期顺序略有差异
    fmt::println("\n--- 3. 多任务并发执行 ---");
    {
        TimerScheduler scheduler(4);  // 4 个工作线程
        scheduler.Start();

        std::mutex mtx;
        std::vector<std::string> log;
        std::condition_variable cv;
        int expected = 5;

        auto start = std::chrono::steady_clock::now();

        // 注册 5 个不同延时的任务
        for (int i = 0; i < 5; ++i) {
            uint64_t delay = 50 + i * 50;  // 50, 100, 150, 200, 250 ms
            scheduler.ScheduleOnce(delay, [&, i, delay]() -> size_t {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    log.push_back(fmt::format("任务 {} (延时{}ms) 在 {}ms 时执行", i, delay, elapsed));
                    if (static_cast<int>(log.size()) == expected) {
                        cv.notify_one();
                    }
                }
                return 0;
            });
        }

        // 等待所有任务完成
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait_for(lk, std::chrono::seconds(2), [&] {
                return static_cast<int>(log.size()) == expected;
            });
        }

        // 打印执行日志
        fmt::println("  执行日志:");
        for (const auto& entry : log) {
            fmt::println("    {}", entry);
        }

        scheduler.Stop();
    }

    fmt::println("\n========================================");
    fmt::println("  TimerScheduler 示例结束");
    fmt::println("========================================");

    return 0;
}
