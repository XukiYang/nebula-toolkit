// =============================================================================
// lockfree_queue_demo.cpp -- LockFreeQueue 无锁队列教学示例
// =============================================================================
//
// 核心思想:
//   SPSC (Single-Producer Single-Consumer) 无锁队列，使用 CAS + acquire/release
//   内存序实现线程间通信，无需互斥锁。
//
// LockFreeQueue 的关键设计:
//   1. 容量必须为 2 的幂 -- 使用位运算 index & mask 替代取模，提升性能
//   2. cache line 对齐 -- read_index_ 和 write_index_ 各占一个 cache line (64字节)
//      避免生产者和消费者之间的 false sharing
//   3. IndexPair 结构 -- {index, cycle} 组合解决 ABA 问题
//   4. 约束 -- T 必须满足 nothrow copy constructible 和 nothrow destructible
//
// =============================================================================

#include <fmt/format.h>

#include <atomic>
#include <containers/lockfree_queue.hpp>
#include <string>
#include <thread>
#include <vector>

int main() {
    using nebula::containers::LockFreeQueue;
    using nebula::containers::Result;

    fmt::println("========================================");
    fmt::println("  LockFreeQueue 无锁队列教学示例");
    fmt::println("========================================\n");

    // 1. 基本 Push / Pop / Peek
    //    Push 成功返回 kSuccess，队列满返回 kErrorFull
    //    Pop 成功返回 kSuccess，队列空返回 kErrorEmpty
    //    Peek 查看队首但不移除
    fmt::println("--- 1. 基本 Push / Pop / Peek ---");
    LockFreeQueue<int> queue(8);  // 容量必须是 2 的幂

    // Push
    for (int i = 1; i <= 5; ++i) {
        auto result = queue.Push(i);
        fmt::println("  Push({}): {} (size={})", i,
                     result == Result::kSuccess ? "OK" : "FULL",
                     queue.Size());
    }

    // Peek
    int peek_val = 0;
    queue.Peek(peek_val);
    fmt::println("  Peek: {} (不移除, size={})", peek_val, queue.Size());

    // Pop
    int val = 0;
    while (queue.Pop(val) == Result::kSuccess) {
        fmt::println("  Pop: {}", val);
    }
    fmt::println("  队列已空: Empty={}", queue.Empty());

    // 2. 批量 PushBulk / PopBulk
    //    批量操作减少原子操作次数，提升吞吐量
    fmt::println("\n--- 2. 批量 PushBulk / PopBulk ---");
    LockFreeQueue<int> bulk_queue(16);

    int batch[] = {10, 20, 30, 40, 50};
    auto push_result = bulk_queue.PushBulk(batch, 5);
    fmt::println("  PushBulk 5个: {} (size={})",
                 push_result == Result::kSuccess ? "OK" : "FAIL",
                 bulk_queue.Size());

    int read_batch[5] = {};
    auto pop_result = bulk_queue.PopBulk(read_batch, 5);
    fmt::println("  PopBulk 5个: {}",
                 pop_result == Result::kSuccess ? "OK" : "FAIL");
    fmt::println("  读取结果: [{}]", fmt::join(std::vector<int>(read_batch, read_batch + 5), ", "));

    // 3. 多线程生产消费验证
    //    典型的 SPSC 模型: 一个线程生产，一个线程消费
    //    无锁设计在高并发下比 mutex 有显著性能优势
    fmt::println("\n--- 3. 多线程生产消费验证 ---");
    constexpr int kItemCount = 100000;
    LockFreeQueue<int> mt_queue(1024);
    std::atomic<bool> producer_done{false};
    std::atomic<int> consumed_count{0};

    // 生产者线程
    std::thread producer([&]() {
        for (int i = 0; i < kItemCount; ++i) {
            while (mt_queue.Push(i) != Result::kSuccess) {
                // 队列满时自旋等待
            }
        }
        producer_done.store(true, std::memory_order_release);
    });

    // 消费者线程
    std::thread consumer([&]() {
        int val = 0;
        while (!producer_done.load(std::memory_order_acquire) || !mt_queue.Empty()) {
            if (mt_queue.Pop(val) == Result::kSuccess) {
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    producer.join();
    consumer.join();

    fmt::println("  生产 {} 个元素", kItemCount);
    fmt::println("  消费 {} 个元素", consumed_count.load());
    fmt::println("  队列最终状态: Empty={}, Size={}", mt_queue.Empty(), mt_queue.Size());

    // 4. 状态查询
    //    Empty/Full/Size/Capacity 提供队列状态
    //    AvaToWrite/AvaToRead 提供可用空间信息
    fmt::println("\n--- 4. 状态查询 ---");
    LockFreeQueue<int> stat_queue(32);
    for (int i = 0; i < 20; ++i) {
        stat_queue.Push(i);
    }

    fmt::println("  Capacity:    {}", stat_queue.Capacity());
    fmt::println("  Size:        {}", stat_queue.Size());
    fmt::println("  AvaToWrite:  {}", stat_queue.AvaToWrite());
    fmt::println("  AvaToRead:   {}", stat_queue.AvaToRead());
    fmt::println("  Empty:       {}", stat_queue.Empty());
    fmt::println("  Full:        {}", stat_queue.Full());

    fmt::println("\n========================================");
    fmt::println("  LockFreeQueue 示例结束");
    fmt::println("========================================");

    return 0;
}
