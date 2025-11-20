#include <iostream>
#include <vector>

#include "../../../include/memory/basic_memory_pool.hpp"
#include "../../../include/memory/scope_guard.hpp"

void TestBasicMemoryPool() {
    memory::BasicMemoryPool pool{10};  // 预分配 10 个块

    // 分配 5 个内存块
    std::vector<void*> allocated;
    for (int i = 0; i < 5; ++i) {
        void* ptr = pool.Allocate();
        std::cout << "Allocated: " << ptr << std::endl;
        allocated.push_back(ptr);
    }

    // 释放前 3 个
    for (int i = 0; i < 3; ++i) {
        std::cout << "Deallocating: " << allocated[i] << std::endl;
        pool.Deallocate(allocated[i]);
    }

    // 再分配 2 个（应该复用刚释放的）
    for (int i = 0; i < 2; ++i) {
        void* ptr = pool.Allocate();
        std::cout << "Re-allocated: " << ptr << std::endl;
    }

    std::cout << "TestBasicMemoryPool completed." << std::endl;
}

void TestScopeGuardWithPool() {
    memory::BasicMemoryPool pool{5};
    std::vector<void*>      ptrs;
    // 分配若干块
    for (int i = 0; i < 3; ++i) {
        ptrs.push_back(pool.Allocate());
    }

    // 创建 guard：确保全部释放
    auto guard = MakeScopeGuard([&]() {
        for (void* p : ptrs) pool.Deallocate(p);
    });

    std::cout << "Using " << ptrs.size() << " blocks\n";
}  // guard 自动清理

int main() {
    TestBasicMemoryPool();
    TestScopeGuardWithPool();
    return 0;
}