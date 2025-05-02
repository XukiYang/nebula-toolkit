#pragma once
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
  size_t Write(const std::vector<uint8_t> &write_data);

  /// @brief 读取数据到std::vector,会根据读取字节开辟vector空间
  /// @param read_data
  /// @param bytes_to_read
  /// @return
  size_t Read(std::vector<uint8_t> &read_data, size_t bytes_to_read);

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

protected:
  /// @brief 检测可写空间
  /// @return 返回可写空间
  size_t AvailableToWrite() const { return buffer_.size() - length_; }

  /// @brief 检测可读空间
  /// @return 返回可读空间
  size_t AvailableToRead() const { return length_; }

protected:
  size_t read_index_ = 0;
  size_t write_index_ = 0;
  size_t length_ = 0;
  std::vector<uint8_t> buffer_;
  std::mutex mutex_;
};

} // namespace containers
