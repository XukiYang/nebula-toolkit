#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace nebula {
namespace memory {

/// @brief 固定块大小的内存池
/// @tparam BlockSize 每个块的字节数，默认 64
template <size_t BlockSize = 64>
class MemoryPool {
public:
    /// @brief 构造内存池
    /// @param initial_block_count 初始块数量
    explicit MemoryPool(size_t initial_block_count = 64) {
        AllocateNewChunk(initial_block_count);
    }

    ~MemoryPool() = default;

    // 禁止拷贝
    MemoryPool(const MemoryPool&)            = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    /// @brief 分配一个或多个块
    /// @param count 块数量
    /// @return 分配的内存指针，失败返回 nullptr
    void* Allocate(size_t count = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        return AllocateLocked(count);
    }

    /// @brief 释放内存
    /// @param ptr 要释放的指针
    /// @return 是否释放成功
    bool Deallocate(void* ptr) {
        if (!ptr) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.push_back({ptr, BlockSize});
        return true;
    }

private:
    struct FreeBlock {
        void*  address;
        size_t size;
    };

    /// @brief 内部分配（持锁调用）
    void* AllocateLocked(size_t count) {
        size_t total_size = count * BlockSize;

        // 查找足够大的空闲块
        for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
            if (it->size >= total_size) {
                void* ptr = it->address;
                if (it->size > total_size) {
                    it->address = static_cast<uint8_t*>(it->address) + total_size;
                    it->size -= total_size;
                } else {
                    free_list_.erase(it);
                }
                return ptr;
            }
        }

        // 没有足够空间，分配新 chunk
        size_t chunk_count = std::max(count, initial_block_count_);
        AllocateNewChunk(chunk_count);
        return AllocateLocked(count);  // 重试（已持锁，不递归加锁）
    }

    void AllocateNewChunk(size_t block_count) {
        size_t chunk_size = block_count * BlockSize;
        chunks_.push_back(std::vector<uint8_t>(chunk_size));
        free_list_.push_back({chunks_.back().data(), chunk_size});
    }

    std::mutex                        mutex_;
    std::vector<std::vector<uint8_t>> chunks_;
    std::vector<FreeBlock>            free_list_;
    size_t                            initial_block_count_ = 64;
};

}  // namespace memory
}  // namespace nebula
