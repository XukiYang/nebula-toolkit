#pragma once
// #include "../logger/logger.hpp" 
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iostream>
#include <mutex>
#include <string.h>
#include <vector>

namespace containers {

/// @brief 环形缓冲区
/// @note 一般线程安全、支持迭代器、0拷贝的线性操作、基本符合google style
class RingBuffer {

public:
  /// @brief 返回结果枚举
  enum class Result {
    kSuccess = 0,     // 成功
    kErrorFull = -1,  // 满
    kErrorEmpty = -2, // 空
    kErrorInvalidSize = -3
  };

public:
  /// @brief 基于实际缓冲区大小构造
  /// @param buffer_size
  explicit RingBuffer(size_t buffer_size) : buffer_(buffer_size){};

  /// @brief 写数据到缓冲区
  /// @param write_data
  /// @return
  template <typename T> size_t Write(const std::vector<T> &write_data) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (write_data.empty())
      return (size_t)Result::kErrorEmpty;

    const size_t available = AvailableToWrite();
    const size_t write_data_size = write_data.size() * sizeof(T);
    // LOG_MSG(available, write_data_size);
    if (write_data_size > available) {
      return (size_t)Result::kErrorFull; // 可写空间不足
    }

    const size_t first_chunk =
        std::min(write_data_size, buffer_.size() - write_index_);

    memcpy(buffer_.data() + write_index_, write_data.data(), first_chunk);
    if (write_data_size > first_chunk) {
      memcpy(buffer_.data(), write_data.data() + first_chunk,
             write_data_size - first_chunk);
    }

    write_index_ = (write_index_ + write_data_size) % buffer_.size();
    length_ += write_data_size;
    return write_data_size;
  };

  /// @brief 读取数据到std::vector
  /// @param read_data
  /// @param bytes_to_read
  /// @return
  template <typename T>
  size_t Read(std::vector<T> &read_data, size_t bytes_to_read) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 过滤非空情况
    if (bytes_to_read == 0)
      return (size_t)Result::kErrorEmpty;

    // 大小必须被整除
    if (bytes_to_read % sizeof(T) != 0) {
      return (size_t)Result::kErrorInvalidSize;
    }

    // 确保有足够空间
    if (read_data.size() * sizeof(T) < bytes_to_read) {
      return (size_t)Result::kErrorInvalidSize;
    }

    // 读取可读空间
    const size_t available = AvailableToRead();
    if (bytes_to_read > available) {
      return (size_t)Result::kErrorFull; // 可读空间不足
    }

    const size_t num_elements = bytes_to_read / sizeof(T);

    // 环回
    const size_t first_chunk =
        std::min(bytes_to_read, buffer_.size() - read_index_);

    // 拷贝到对应的地址
    memcpy(read_data.data(), buffer_.data() + read_index_, first_chunk);

    // 如果读取字节大于线性第一块，环回
    if (bytes_to_read > first_chunk) {
      memcpy(read_data.data() + (first_chunk / sizeof(T)), buffer_.data(),
             bytes_to_read - first_chunk);
    }

    read_index_ = (read_index_ + bytes_to_read) % buffer_.size();

    length_ -= bytes_to_read;
    return bytes_to_read;
  };

  /// @brief 读取指定字节数据到std::byte *
  /// @param read_ptr
  /// @param bytes_to_read
  /// @return
  size_t Read(std::byte *read_ptr, size_t bytes_to_read);

  /// @brief 写入std::byte *指定字节数据
  /// @param write_ptr
  /// @param bytes_to_write
  /// @return
  size_t Write(const std::byte *write_ptr, size_t bytes_to_write);

  // /// @brief 按照kb读取数据
  // /// @param read_ptr
  // /// @param kbs
  // /// @return
  // size_t Read(std::byte *read_ptr, uint8_t kbs);

public:
  /// @brief hex打印缓冲区
  void PrintBuffer();

  /// @brief 动态更新缓冲区容器实际大小
  /// @return
  size_t Resize(size_t buffer_size);

  /// @brief 返回缓冲区容器实际大小
  /// @return
  size_t Capacity() const { return buffer_.size(); };

  /// @brief 清空缓冲区
  /// @return
  bool Clear();

  /// @brief 仅读数据，不出队
  /// @return 读取是否成功
  size_t Peek(std::vector<uint8_t> &read_data, size_t bytes_to_read);

public:
  /// 迭代器指针使用const保护,避免非法操作

  /// @brief 迭代器 begin
  /// @return
  const uint8_t *begin() { return buffer_.data() + read_index_; }

  /// @brief 迭代器 end
  /// @return
  const uint8_t *end() { return buffer_.data() + write_index_; }

  /// @brief 返回指针
  /// @return
  const uint8_t *data() { return buffer_.data() + read_index_; }

public:
  /// @brief 获取当前已经存储的字节数
  /// @return
  size_t Length() const { return length_; }

  /// @brief 缓冲区是否空
  /// @return
  bool IsEmpty() const { return length_ == 0; }

  /// @brief 缓冲区是否满
  /// @return
  bool IsFull() const { return length_ == buffer_.size(); };

  /// @brief 容量使用率
  /// @return 使用率
  float Usage() const { return static_cast<float>(length_) / buffer_.size(); }

public:
  /// @brief 检测可写空间
  /// @return 返回可写空间
  size_t AvailableToWrite() const { return buffer_.size() - length_; }

  /// @brief 检测可读空间
  /// @return 返回可读空间
  size_t AvailableToRead() const { return length_; }

public:
  /// ​完全避免内存拷贝，保持线程安全，​无缝对接系统调用

  /// @brief 获取线性可写空间
  /// @return 指针，可写字节数
  std::pair<uint8_t *, size_t> GetLinearWriteSpace();

  /// @brief 提交线性写入字节数
  /// @param write_size
  /// @return
  Result CommitWriteSize(size_t write_size);

  /// @brief 获取线性可读空间
  /// @return
  std::pair<const uint8_t *, size_t> GetLinearReadSpace();

  /// @brief 提交线性读取字节数
  /// @param write_size
  /// @return
  Result CommitReadSize(size_t read_size);

public:
  size_t read_index_ = 0;
  size_t write_index_ = 0;
  size_t length_ = 0;
  std::vector<uint8_t> buffer_;
  std::mutex mutex_;
};

} // namespace containers
