#pragma once
#include <memory>
#include <mutex>
#include <vector>

/* 基于C++11的线程安全的内存池 */
namespace nebula {
namespace memory {
class BasicMemoryPool {
    struct Block {
        char   data[64];
        Block* next;
    };

    std::mutex                            mutex_;
    Block*                                free_list_ = nullptr;
    std::vector<std::unique_ptr<Block[]>> chunks_;

private:
    /* 构建指定数量内存块 */
    void AllocateChunk(size_t block_count) {
        /* 根据构建数量申请到批量块大小的内存空间 */
        auto chunk = std::make_unique<Block[]>(block_count);
        /* 切分内存空间为块并头插到链表 LIFO */
        for (size_t i = 0; i < block_count; i++) {
            chunk[i].next = free_list_;
            free_list_    = &chunk[i];
        }
        /* 保存创建的内存块引用防止释放 */
        chunks_.push_back(std::move(chunk));
    }

public:
    BasicMemoryPool(size_t block_count = 64) {
        AllocateChunk(block_count);
    };
    ~BasicMemoryPool() = default;

    /* 分配内存 */
    void* Allocate(size_t block_count = 64) {
        std::lock_guard<std::mutex> lock(mutex_);
        /* 为空就分配新的内存块 */
        if (free_list_ == nullptr) {
            AllocateChunk(block_count);
        }
        /* 取出链表头的块 */
        Block* block = free_list_;
        free_list_   = block->next;
        return static_cast<void*>(block);
    };

    /* 释放内存 */
    bool Deallocate(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        Block*                      block = static_cast<Block*>(ptr);
        block->next                       = free_list_;
        free_list_                        = block;
    };
};

}  // namespace memory
}  // namespace nebula
