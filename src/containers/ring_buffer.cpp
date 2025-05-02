#include "../../include/containers/ring_buffer.hpp"

size_t containers::RingBuffer::Write(const std::vector<uint8_t> &write_data) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (write_data.empty())
    return (size_t)Result::kErrorEmpty;

  const size_t available = AvailableToWrite();

  if (write_data.size() > available) {
    return (size_t)Result::kErrorFull; // 可写空间不足
  }

  const size_t first_chunk =
      std::min(write_data.size(), buffer_.size() - write_index_);

  memcpy(buffer_.data() + write_index_, write_data.data(), first_chunk);
  if (write_data.size() > first_chunk) {
    memcpy(buffer_.data(), write_data.data() + first_chunk,
           write_data.size() - first_chunk);
  }

  write_index_ = (write_index_ + write_data.size()) % buffer_.size();
  length_ += write_data.size();
  return write_data.size();
}

size_t containers::RingBuffer::Read(std::vector<uint8_t> &read_data,
                                    size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (bytes_to_read == 0)
    return (size_t)Result::kErrorEmpty;

  const size_t available = AvailableToRead();
  if (bytes_to_read > available) {
    return (size_t)Result::kErrorFull; // 可读空间不足
  }

  read_data.resize(bytes_to_read);

  const size_t first_chunk =
      std::min(bytes_to_read, buffer_.size() - read_index_);

  memcpy(read_data.data(), buffer_.data() + read_index_, first_chunk);

  if (bytes_to_read > first_chunk) {
    memcpy(read_data.data() + first_chunk, buffer_.data(),
           bytes_to_read - first_chunk);
  }

  read_index_ = (read_index_ + bytes_to_read) % buffer_.size();
  length_ -= bytes_to_read;
  return bytes_to_read;
}

void containers::RingBuffer::PrintBuffer() {
  std::ios_base::fmtflags original_flags = std::cout.flags();
  std::cout << "-- byte stream --" << std::endl;
  std::cout << std::hex << std::setfill('0');
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &item : buffer_) {
    std::cout << std::setw(2) << static_cast<int>(item) << " ";
  }
  std::cout << std::endl << "-----------------" << std::endl;
  std::cout.flags(original_flags);
}

size_t containers::RingBuffer::Resize(size_t buffer_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  buffer_.resize(buffer_size);
  return buffer_.size();
}

bool containers::RingBuffer::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  read_index_ = 0;
  write_index_ = 0;
  length_ = 0;
  return true;
}

size_t containers::RingBuffer::Peek(std::vector<uint8_t> &read_data,
                                    size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (bytes_to_read == 0)
    return 0;

  const size_t available = AvailableToRead();
  if (bytes_to_read > available) {
    return -1; // 可读数据不足
  }

  read_data.resize(bytes_to_read);

  const size_t first_chunk =
      std::min(bytes_to_read, buffer_.size() - read_index_);

  memcpy(read_data.data(), buffer_.data() + read_index_, first_chunk);

  if (bytes_to_read > first_chunk) {
    memcpy(read_data.data() + first_chunk, buffer_.data(),
           bytes_to_read - first_chunk);
  }
  // 不变动读标志与已读标志
  return bytes_to_read;
}
