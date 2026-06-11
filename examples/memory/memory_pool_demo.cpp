// =============================================================================
// memory_pool_demo.cpp -- MemoryPool 固定块内存池教学示例
// =============================================================================
//
// 核心思想:
//   固定块内存池预先分配大块内存，然后以固定大小的小块进行分配/释放。
//   相比频繁调用 new/delete，内存池有以下优势:
//   1. 分配速度快 -- 从空闲链表取块，O(1) 操作
//   2. 无内存碎片 -- 所有块大小相同，释放后可立即复用
//   3. 线程安全 -- 内部使用 mutex 保护分配/释放操作
//
// 适用场景:
//   - 高频分配/释放相同大小对象（如网络消息、事件对象）
//   - 需要减少堆分配开销的性能敏感场景
//
// =============================================================================

#include <fmt/format.h>

#include <memory/memory_pool.hpp>
#include <vector>

int main() {
    using nebula::memory::MemoryPool;

    fmt::println("========================================");
    fmt::println("  MemoryPool 固定块内存池教学示例");
    fmt::println("========================================\n");

    // 1. 基本分配与释放
    //    MemoryPool<64> 表示每个块 64 字节
    //    构造参数指定初始块数量，池会自动扩展
    fmt::println("--- 1. 基本分配与释放 ---");
    MemoryPool<64> pool(8);  // 每块 64 字节，初始 8 块

    // 分配单个块
    void* p1 = pool.Allocate();
    fmt::println("  分配 p1: {} (单块, 64字节)", fmt::ptr(p1));

    void* p2 = pool.Allocate();
    fmt::println("  分配 p2: {}", fmt::ptr(p2));

    // 分配连续多块
    void* p3 = pool.Allocate(3);  // 分配 3 个连续块 = 192 字节
    fmt::println("  分配 p3: {} (3块, 192字节)", fmt::ptr(p3));

    // 释放
    pool.Deallocate(p1);
    fmt::println("  释放 p1: OK");
    pool.Deallocate(p2);
    fmt::println("  释放 p2: OK");
    pool.Deallocate(p3);
    fmt::println("  释放 p3: OK");

    // 2. 批量分配
    //    连续分配多个块，验证地址连续性
    fmt::println("\n--- 2. 批量分配 ---");
    MemoryPool<32> small_pool(16);
    std::vector<void*> ptrs;

    for (int i = 0; i < 10; ++i) {
        void* p = small_pool.Allocate();
        ptrs.push_back(p);
    }

    fmt::println("  分配 10 个块:");
    for (size_t i = 0; i < ptrs.size(); ++i) {
        fmt::println("    [{}] {}", i, fmt::ptr(ptrs[i]));
    }

    // 释放所有
    for (auto* p : ptrs) {
        small_pool.Deallocate(p);
    }
    fmt::println("  全部释放完成");

    // 3. 释放后复用验证
    //    释放的块会被放回空闲链表，后续分配时复用
    //    这是内存池的核心优势 -- 避免频繁向系统申请/归还内存
    fmt::println("\n--- 3. 释放后复用验证 ---");
    MemoryPool<64> reuse_pool(2);  // 初始 2 块

    // 分配 2 个块（用完初始块）
    void* a = reuse_pool.Allocate();
    void* b = reuse_pool.Allocate();
    fmt::println("  分配: a={}, b={}", fmt::ptr(a), fmt::ptr(b));

    // 释放 a
    reuse_pool.Deallocate(a);
    fmt::println("  释放 a");

    // 再分配 -- 应该复用 a 的地址（空闲链表中第一个满足大小的块）
    void* c = reuse_pool.Allocate();
    fmt::println("  分配 c={} (应复用 a 的地址)", fmt::ptr(c));
    fmt::println("  复用验证: {}", (c == a) ? "PASS -- 地址相同" : "FAIL");

    // 清理
    reuse_pool.Deallocate(b);
    reuse_pool.Deallocate(c);

    fmt::println("\n========================================");
    fmt::println("  MemoryPool 示例结束");
    fmt::println("========================================");

    return 0;
}
