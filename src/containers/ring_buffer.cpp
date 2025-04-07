/*
 * @Descripttion:
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-04-07 17:08:30
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-04-07 17:18:47
 */
#include "../../include/containers/ring_buffer.hpp"

containers::RingBuffer::RingBuffer(size_t buffer_size) : buffer_(buffer_size) {}

int containers::RingBuffer::WriteData(const std::vector<uint8_t> &write_data) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (write_data.empty())
    return 0;

  const size_t available = AvailableToWrite();
  if (write_data.size() > available) {
    return -1; // Not enough space
  }

  const size_t first_chunk =
      std::min(write_data.size(), buffer_.size() - write_index_);
  std::copy(write_data.begin(), write_data.begin() + first_chunk,
            buffer_.begin() + write_index_);

  if (write_data.size() > first_chunk) {
    std::copy(write_data.begin() + first_chunk, write_data.end(),
              buffer_.begin());
  }

  write_index_ = (write_index_ + write_data.size()) % buffer_.size();
  length_ += write_data.size();
  return write_data.size();
}

int containers::RingBuffer::ReadData(std::vector<uint8_t> *read_data,
                                     size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (bytes_to_read == 0)
    return 0;

  const size_t available = AvailableToRead();
  if (bytes_to_read > available) {
    return -1; // Not enough data
  }

  read_data->resize(bytes_to_read);
  const size_t first_chunk =
      std::min(bytes_to_read, buffer_.size() - read_index_);
  std::copy(buffer_.begin() + read_index_,
            buffer_.begin() + read_index_ + first_chunk, read_data->begin());

  if (bytes_to_read > first_chunk) {
    std::copy(buffer_.begin(), buffer_.begin() + (bytes_to_read - first_chunk),
              read_data->begin() + first_chunk);
  }

  read_index_ = (read_index_ + bytes_to_read) % buffer_.size();
  length_ -= bytes_to_read;
  return bytes_to_read;
}