/*
 * @Descripttion:环形缓冲区
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-04-07 16:33:17
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-04-08 14:54:10
 */
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <iostream>
#include <mutex>
#include <vector>

namespace containers {

class RingBuffer {
public:
  explicit RingBuffer(size_t buffer_size);
  int WriteData(const std::vector<uint8_t> &write_data);
  int ReadData(std::vector<uint8_t> *read_data, size_t bytes_to_read);
  void PrintBuffer();

  size_t Capacity() const { return buffer_.size(); }
  size_t Size() const { return length_; }
  bool IsEmpty() const { return length_ == 0; }
  bool IsFull() const { return length_ == buffer_.size(); }

private:
  size_t AvailableToWrite() const { return buffer_.size() - length_; }
  size_t AvailableToRead() const { return length_; }

private:
  size_t read_index_ = 0;
  size_t write_index_ = 0;
  size_t length_ = 0;
  std::vector<uint8_t> buffer_;
  std::mutex mutex_;
};

} // namespace containers
