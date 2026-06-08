#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>

/* std::memory_order 语义说明：
 *
 * memory_order_relaxed    : 仅保证原子性，无同步和顺序约束，性能最高。
 * memory_order_acquire    : 读操作（load）使用，防止后续内存访问重排到该操作之前；
 *                          通常与 release 配对，用于“获取”共享数据。
 * memory_order_release    : 写操作（store）使用，防止先前内存访问重排到该操作之后；
 *                          通常与 acquire 配对，用于“发布”共享数据。
 * memory_order_acq_rel    : 用于读-改-写操作（如 exchange、compare_exchange、fetch_add），
 *                          同时具备 acquire 和 release 语义。
 * memory_order_seq_cst    : 顺序一致性模型，默认内存序；
 *                          提供最强的全局顺序保证，但性能开销最大。
 */

// 示例 1: memory_order_relaxed —— 独立计数器
void example_relaxed() {
    std::cout << "\n=== Example 1: memory_order_relaxed ===\n";
    std::atomic<int> counter{0};

    auto worker = [&]() {
        for (int i = 0; i < 1000; ++i) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(worker);

    for (auto& t : threads) t.join();

    int result = counter.load(std::memory_order_relaxed);
    std::cout << "Final counter (should be 4000): " << result << "\n";
}

// 示例 2: release + acquire —— 发布/消费同步
void example_release_acquire() {
    std::cout << "\n=== Example 2: release + acquire ===\n";
    std::atomic<bool> ready{false};
    int               data = 0;

    auto producer = [&]() {
        data = 42;                                     // 写共享数据
        ready.store(true, std::memory_order_release);  // 发布
    };

    auto consumer = [&]() {
        while (!ready.load(std::memory_order_acquire)) {
            // busy wait
        }
        assert(data == 42);  // 必须成功！
        std::cout << "Consumer saw data = " << data << "\n";
    };

    std::thread t1(producer);
    std::thread t2(consumer);
    t1.join();
    t2.join();
    std::cout << "Synchronization successful!\n";
}

// 示例 3: memory_order_seq_cst —— 全局顺序
void example_seq_cst() {
    std::cout << "\n=== Example 3: memory_order_seq_cst ===\n";
    std::atomic<bool> x{false}, y{false};
    std::atomic<int>  turn{0};  // 0=none, 1=thread1 thinks y not set, 2=thread2 thinks x not set

    auto thread1 = [&]() {
        x.store(true, std::memory_order_seq_cst);
        if (!y.load(std::memory_order_seq_cst)) {
            turn = 1;
        }
    };

    auto thread2 = [&]() {
        y.store(true, std::memory_order_seq_cst);
        if (!x.load(std::memory_order_seq_cst)) {
            turn = 2;
        }
    };

    std::thread t1(thread1), t2(thread2);
    t1.join();
    t2.join();

    int final_turn = turn.load();
    std::cout << "Turn value: " << final_turn << "\n";
    // 在 seq_cst 下，不可能出现 turn == 0 且双方都进入 if（即不会同时设为1和2）
    // 实际上，turn 可能是 0（双方都看到对方已设置）、1 或 2，但逻辑一致。
}

// 示例 4: memory_order_acq_rel —— 自旋锁中的 RMW
class SpinLock {
    std::atomic<bool> flag{false};

public:
    void lock() {
        while (flag.exchange(true, std::memory_order_acq_rel)) {
            // 自旋
        }
    }
    void unlock() {
        flag.store(false, std::memory_order_release);
    }
};

void example_acq_rel() {
    std::cout << "\n=== Example 4: memory_order_acq_rel (SpinLock) ===\n";
    SpinLock         lock;
    std::atomic<int> shared_value{0};

    auto worker = [&](int id) {
        for (int i = 0; i < 100; ++i) {
            lock.lock();
            shared_value.fetch_add(1, std::memory_order_relaxed);
            lock.unlock();
        }
    };

    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();
    t2.join();

    std::cout << "Final shared_value (should be 200): " << shared_value.load() << "\n";
}

// 主函数
int main() {
    std::cout << "C++ Memory Order Demo\n";
    std::cout << "=====================\n";

    try {
        // example_relaxed();
        // example_release_acquire();
        example_seq_cst();
        // example_acq_rel();
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    std::cout << "\n All examples completed successfully!\n";
    return 0;
}