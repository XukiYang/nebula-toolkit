/* 基于C++11的单生产者单消费者线程安全的无锁环形队列实现 */
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#define ALIGNAS_SIZE 64

namespace nebula {
namespace containers {

enum class Result { kSuccess = 0, kErrorFull = -1, kErrorEmpty = -2, kErrorInvalidSize = -3, kInvalidParam = -4 };

template <typename T>
class LockFreeQueue {
private:
    static_assert(std::is_nothrow_copy_constructible<T>::value, "T must be nothrow copy constructible");
    static_assert(std::is_nothrow_destructible<T>::value, "T must be nothrow destructible");

    struct alignas(ALIGNAS_SIZE) IndexPair {
        size_t index;
        size_t cycle;
    };

    alignas(ALIGNAS_SIZE) std::atomic<IndexPair> read_index_{{0, 0}};
    alignas(ALIGNAS_SIZE) std::atomic<IndexPair> write_index_{{0, 0}};

    const size_t         capacity_;
    const size_t         mask_;
    std::unique_ptr<T[]> data_ptr_;

    size_t PhysicalIndex(size_t logical_index) const noexcept {
        return logical_index & mask_;
    }

    size_t NextCycle(size_t current_cycle, size_t old_index, size_t new_index) const noexcept {
        return current_cycle + (new_index - old_index >= capacity_ ? 1 : 0);
    }

public:
    explicit LockFreeQueue(size_t queue_size) : capacity_(queue_size), mask_(queue_size - 1) {
        if (queue_size == 0 || (queue_size & (queue_size - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of two and > 0");
        }
        data_ptr_.reset(new T[queue_size]);
    }

    ~LockFreeQueue() {
        T item;
        while (Pop(item) == Result::kSuccess) {
        }
    }

    LockFreeQueue(const LockFreeQueue &) = delete;
    LockFreeQueue &operator=(const LockFreeQueue &) = delete;

    bool Empty() const noexcept {
        auto r = read_index_.load(std::memory_order_acquire);
        auto w = write_index_.load(std::memory_order_acquire);
        return r.index == w.index;
    }

    bool Full() const noexcept {
        auto w = write_index_.load(std::memory_order_acquire);
        auto r = read_index_.load(std::memory_order_acquire);
        return (w.index - r.index) >= capacity_;
    }

    size_t Size() const noexcept {
        auto   r    = read_index_.load(std::memory_order_acquire);
        auto   w    = write_index_.load(std::memory_order_acquire);
        size_t diff = w.index - r.index;
        if (diff > capacity_) return capacity_;
        return diff;
    }

    size_t Capacity() const noexcept {
        return capacity_;
    }

    size_t AvaToWrite() const noexcept {
        auto w = write_index_.load(std::memory_order_acquire);
        auto r = read_index_.load(std::memory_order_acquire);

        size_t diff = w.index - r.index;
        if (diff >= capacity_) {
            return 0;
        }
        return capacity_ - diff;
    }

    size_t AvaToRead() const noexcept {
        auto w = write_index_.load(std::memory_order_acquire);
        auto r = read_index_.load(std::memory_order_acquire);

        size_t diff = w.index - r.index;
        if (diff > capacity_) {
            return capacity_;
        }
        return diff;
    }

    Result Push(const T &item) {
        return PushBulk(&item, 1);
    }

    Result PushBulk(const T *items, size_t count) {
        if (!items || count == 0) {
            return Result::kErrorInvalidSize;
        }
        if (count > capacity_) {
            return Result::kErrorFull;
        }

        IndexPair current_write = write_index_.load(std::memory_order_relaxed);
        IndexPair current_read;
        size_t    avail;
        size_t    new_index, new_cycle;
        do {
            current_read = read_index_.load(std::memory_order_acquire);

            size_t diff = current_write.index - current_read.index;
            if (diff >= capacity_) {
                return Result::kErrorFull;
            }

            avail = capacity_ - diff;
            if (count > avail) {
                return Result::kErrorFull;
            }

            new_index = current_write.index + count;
            new_cycle = NextCycle(current_write.cycle, current_read.index, new_index);

        } while (!write_index_.compare_exchange_weak(current_write, IndexPair{new_index, new_cycle},
                                                     std::memory_order_acq_rel, std::memory_order_relaxed));

        for (size_t i = 0; i < count; ++i) {
            size_t pos     = PhysicalIndex(current_write.index + i);
            data_ptr_[pos] = items[i];
        }

        return Result::kSuccess;
    }

    Result Pop(T &item) {
        return PopBulk(&item, 1);
    }

    Result Pop() {
        T temp;
        return PopBulk(&temp, 1);
    };

    Result PopBulk(T *output, size_t count) {
        if (!output || count == 0) {
            return Result::kErrorInvalidSize;
        }

        IndexPair current_read = read_index_.load(std::memory_order_relaxed);
        IndexPair current_write;
        size_t    avail;
        size_t    new_index, new_cycle;
        do {
            current_write = write_index_.load(std::memory_order_acquire);

            size_t diff = current_write.index - current_read.index;
            if (diff == 0) {
                return Result::kErrorEmpty;
            }

            avail = diff;
            if (count > avail) {
                return Result::kErrorEmpty;
            }

            new_index = current_read.index + count;
            new_cycle = NextCycle(current_read.cycle, current_write.index, new_index);

        } while (!read_index_.compare_exchange_weak(current_read, IndexPair{new_index, new_cycle},
                                                    std::memory_order_acq_rel, std::memory_order_relaxed));

        for (size_t i = 0; i < count; ++i) {
            size_t pos = PhysicalIndex(current_read.index + i);
            output[i]  = std::move(data_ptr_[pos]);
        }

        return Result::kSuccess;
    }

    Result Peek(T &item) const {
        return PeekBulk(&item, 1);
    }

    Result PeekBulk(T *output, size_t count) const {
        if (!output || count == 0) {
            return Result::kErrorInvalidSize;
        }

        IndexPair r = read_index_.load(std::memory_order_acquire);
        IndexPair w = write_index_.load(std::memory_order_acquire);

        size_t diff = w.index - r.index;
        if (count > diff) {
            return Result::kErrorEmpty;
        }

        for (size_t i = 0; i < count; ++i) {
            size_t pos = PhysicalIndex(r.index + i);
            output[i]  = data_ptr_[pos];
        }

        return Result::kSuccess;
    }

    Result Clear() {
        read_index_  = {0, 0};
        write_index_ = {0, 0};
        return Result::kSuccess;
    }
};
}  // namespace containers
}  // namespace nebula