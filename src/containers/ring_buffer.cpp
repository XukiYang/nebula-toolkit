#include "../../include/containers/ring_buffer.hpp"

size_t containers::RingBuffer::Read(std::byte *read_ptr, size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (bytes_to_read == 0)
    return (size_t)Result::kErrorEmpty;
  const size_t available = AvailableToRead();
  if (bytes_to_read > available) {
    return (size_t)Result::kErrorFull; // 可读空间不足
  }

  const size_t first_chunk =
      std::min(bytes_to_read, buffer_.size() - read_index_);

  memcpy(read_ptr, buffer_.data() + read_index_, first_chunk);

  if (bytes_to_read > first_chunk) {
    memcpy(read_ptr + first_chunk, buffer_.data(), bytes_to_read - first_chunk);
  }

  read_index_ = (read_index_ + bytes_to_read) % buffer_.size();
  length_ -= bytes_to_read;
  return bytes_to_read;
}

size_t containers::RingBuffer::Write(const std::byte *write_ptr,
                                     size_t bytes_to_write) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (bytes_to_write == 0) {
    return (size_t)Result::kErrorEmpty;
  }

  const size_t available = AvailableToWrite();

  if (bytes_to_write > available) {
    return (size_t)Result::kErrorFull; // 可写空间不足
  }

  const size_t first_chunk =
      std::min(bytes_to_write, buffer_.size() - write_index_);

  memcpy(buffer_.data() + write_index_, write_ptr, first_chunk);
  if (bytes_to_write > first_chunk) {
    memcpy(buffer_.data(), write_ptr + first_chunk,
           bytes_to_write - first_chunk);
  }
  write_index_ = (write_index_ + bytes_to_write) % buffer_.size();
  length_ += bytes_to_write;
  return bytes_to_write;
}

void containers::RingBuffer::PrintBuffer() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ios_base::fmtflags original_flags = std::cout.flags();

  std::cout << "┌──────────────────────────────────────┐\n";
  std::cout << "│ Ring Buffer [R:" << std::setw(2) << read_index_
            << " W:" << std::setw(2) << write_index_ << " L:" << std::setw(2)
            << length_ << "] │\n";
  std::cout << "├──────────────────────────────────────┤\n";

  std::cout << "│ ";
  std::cout << std::hex << std::setfill('0');
  for (size_t i = 0; i < buffer_.size(); ++i) {
    std::cout << std::setw(2) << static_cast<int>(buffer_[i]) << " ";
    if ((i + 1) % 8 == 0 && (i + 1) != buffer_.size()) {
      std::cout << "│\n│ ";
    }
  }
  size_t remaining = 8 - (buffer_.size() % 8 ? buffer_.size() % 8 : 8);
  for (size_t i = 0; i < remaining; ++i) {
    std::cout << "   ";
  }

  std::cout << "│\n";
  std::cout << "└──────────────────────────────────────┘\n";
  std::cout.flags(original_flags);
}

size_t containers::RingBuffer::Resize(size_t buffer_size) {
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

std::pair<uint8_t *, size_t> containers::RingBuffer::GetLinearWriteSpace() {
  std::lock_guard<std::mutex> lock(mutex_);
  // 线性可写字节数:size()-write_index
  // 线性写入指针:data()+write_index_
  size_t linear_space =
      std::min(AvailableToWrite(), buffer_.size() - write_index_);
  return {buffer_.data() + write_index_, linear_space};
}

containers::RingBuffer::Result
containers::RingBuffer::CommitWriteSize(size_t write_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (write_size > AvailableToWrite()) {
    return Result::kErrorInvalidSize;
  }
  // 同步写位置(环回)以及使用容量
  write_index_ = (write_index_ + write_size) % buffer_.size();
  length_ += write_size;
  return Result::kSuccess;
}

std::pair<const uint8_t *, size_t>
containers::RingBuffer::GetLinearReadSpace() {
  std::lock_guard<std::mutex> lock(mutex_);
  // 线性可读字节数:size()-read_index
  // 线性读取指针:data()+read_index
  size_t linear_space =
      std::min(AvailableToRead(), buffer_.size() - read_index_);
  return {buffer_.data() + read_index_, linear_space};
}

containers::RingBuffer::Result
containers::RingBuffer::CommitReadSize(size_t read_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (read_size > AvailableToRead()) {
    return Result::kErrorInvalidSize;
  }
  // 同步读位置(环回)以及使用容量
  read_index_ = (read_index_ + read_size) % buffer_.size();
  length_ -= read_size;
  return Result::kSuccess;
};