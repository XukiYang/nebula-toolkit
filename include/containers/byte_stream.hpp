/*
 * @Descripttion:字节序列化
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-04-08 09:11:18
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-04-08 15:50:12
 */
#pragma once
#include "ring_buffer.hpp"
#include <memory>
#include <string.h>
#include <string>
#include <type_traits>
#include <vector>

namespace containers {

class ByteStream {
private:
  std::unique_ptr<RingBuffer> ring_buffer_;

public:
  ByteStream(size_t buffer_size);
  ~ByteStream() = default;

public:
  int WriteRaw(const void *data, size_t size);

  int ReadRaw(void *data, size_t size);

  template <typename T> ByteStream &operator>>(T &data) {
    ReadRaw(&data, sizeof(T));
    return *this;
  };

  template <typename T> ByteStream &operator<<(const T &data) {
    WriteRaw(&data, sizeof(T));
    return *this;
  };

  ByteStream &operator<<(const std::string &data);

  ByteStream &operator>>(std::string &data);

  template <typename T> ByteStream &operator<<(const std::vector<T> &data) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");   
    WriteRaw(data.data(), data.size() * sizeof(T));
    return *this;
  }

  template <typename T> ByteStream &operator>>(std::vector<T> &data) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "Type must be trivially copyable");
    size_t available = ring_buffer_->Size();
    if (available % sizeof(T) != 0) {
      return *this;
    }
    data.resize(available / sizeof(T));
    ReadRaw(data.data(), available);
    return *this;
  }

public:
  void PrintBuffer() { ring_buffer_->PrintBuffer(); }
  size_t Capacity() const { return ring_buffer_->Capacity(); }
  size_t Size() const { return ring_buffer_->Size(); }
  bool IsEmpty() const { return ring_buffer_->IsEmpty(); }
  bool IsFull() const { return ring_buffer_->IsFull(); }
};
} // namespace containers
