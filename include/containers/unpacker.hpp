
#pragma once
#include "../logkit/logkit.hpp"
#include "ring_buffer.hpp"
#include <functional>

namespace containers {

using HeadSzCb = std::function<size_t(void)>; // 头结构体字节
using TailSzCb = std::function<size_t(void)>; // 尾结构体字节
using HeadKey = std::vector<uint8_t>;         // 头定位符
using TailKey = std::vector<uint8_t>;         // 尾定位符

class UnPacker : public RingBuffer {
  enum UnPackerResult { kSuccess = 0, kError = -1 };

public:
  UnPacker(HeadSzCb head_type_cb, TailSzCb tail_type_cb, HeadKey &&head_key,
           TailKey &&tail_key, size_t buffer_size = 1024)
      : RingBuffer(buffer_size), head_type_cb_(head_type_cb),
        tail_type_cb_(tail_type_cb), head_key_(std::move(head_key)),
        tail_key_(std::move(tail_key)) {
    if (!head_type_cb_ || !tail_type_cb_) {
      throw std::invalid_argument("Callback functions cannot be null");
    }
  }

  /// @brief
  /// @param write_data
  /// @param data_size
  /// @param read_data
  /// @return
  size_t PushAndGet(const uint8_t *write_data, size_t data_size,
                    std::vector<std::vector<uint8_t>> &read_data) {
    if (write_data == nullptr) {
      return UnPackerResult::kError;
    }
    size_t write_ret =
        Write(reinterpret_cast<const std::byte *>(write_data), data_size);
    LOGP_DEBUG("write_ret=%d,AvailableToRead:%d", write_ret, AvailableToRead());
    GetPack(read_data);
    return write_ret;
  };

private:
  /// @brief
  /// @param read_data
  /// @return
  UnPackerResult GetPack(std::vector<std::vector<uint8_t>> &read_data) {

    // 仅头定位符（包含头定位符）
    if (!head_key_.empty() && tail_key_.empty()) {

      // 查找头定位符
      size_t first_head_pos = FindKey(head_key_);
      LOGP_DEBUG("first_head_pos:%d", first_head_pos);
      if (first_head_pos == buffer_.size()) {
        return UnPackerResult::kError;
      }
      while (AvailableToRead()) {
        // 查找下一个头定位符
        size_t second_head_pos =
            FindKey(head_key_, first_head_pos + head_key_.size());
        LOGP_DEBUG("second_head_pos:%d", second_head_pos);

        // 下一个头定位符到达缓冲区末尾
        if (second_head_pos == buffer_.size()) {
          return UnPackerResult::kError;
        }

        // 计算包含定位符的完整一包数据
        size_t data_len = second_head_pos - first_head_pos;
        std::vector<uint8_t> packet(data_len);

        // 计算线性可读空间
        size_t first_chunk =
            std::min(data_len, buffer_.size() - first_head_pos);
        memcpy(packet.data(), buffer_.data() + first_head_pos, first_chunk);

        // 如果包数据超过了线性可读空间，则发生换回
        if (data_len > first_chunk) {
          // 拷贝环回数据
          memcpy(packet.data() + first_chunk, buffer_.data(),
                 data_len - first_chunk);
        }

        read_data.emplace_back(std::move(packet));
        CommitReadSize(data_len);

        first_head_pos = second_head_pos;
      }

      return UnPackerResult::kSuccess;
    }
    // 头和尾定位符（包含头尾定位符）
    else if (!head_key_.empty() && !tail_key_.empty()) {
      // 查找头定位符
      size_t head_pos = FindKey(head_key_);
      LOGP_DEBUG("head_pos:%d", head_pos);
      if (head_pos == buffer_.size()) {
        return UnPackerResult::kError;
      }
      while (AvailableToRead()) {
        // 查找下一个头定位符
        size_t tail_pos = FindKey(tail_key_, head_pos + head_key_.size());
        LOGP_DEBUG("tail_pos:%d", tail_pos);

        // 下一个头定位符到达缓冲区末尾
        if (head_pos == buffer_.size()) {
          return UnPackerResult::kError;
        }
        // 计算包含定位符的完整一包数据
        size_t data_len = tail_pos - head_pos + tail_key_.size();
        std::vector<uint8_t> packet(data_len);

        // 计算线性可读空间
        size_t first_chunk = std::min(data_len, buffer_.size() - tail_pos);
        memcpy(packet.data(), buffer_.data() + tail_pos + tail_key_.size(),
               first_chunk);

        // 如果包数据超过了线性可读空间，则发生换回
        if (data_len > first_chunk) {
          // 拷贝环回数据
          memcpy(packet.data() + first_chunk, buffer_.data(),
                 data_len - first_chunk);
        }

        head_pos = FindKey(head_key_, tail_pos + tail_key_.size());
        LOGP_MSG("head_pos:%d", head_pos);
        if (head_pos == buffer_.size()) {
          return UnPackerResult::kError;
        }
        read_data.emplace_back(std::move(packet));
        CommitReadSize(data_len);
      }
      return UnPackerResult::kSuccess;
    }
    // 其他
    else {
      LOG_DEBUG("头尾标记为空");
      return UnPackerResult::kError;
    }
  }

  /// @brief 在环形缓冲区中查找关键字节序列
  /// @param find_key 要查找的关键字节序列
  /// @param start_index 起始搜索位置（绝对位置）
  /// @return 找到返回绝对位置，未找到返回buffer_.size()
  size_t FindKey(const std::vector<uint8_t> &find_key, size_t start_index = 0) {
    if (find_key.empty() || start_index >= buffer_.size() ||
        find_key.size() > AvailableToRead()) {
      return buffer_.size();
    }

    const size_t key_len = find_key.size();
    if (key_len > AvailableToRead()) {
      return buffer_.size();
    }

    // 获取线性可读空间（考虑环回）
    auto [ptr, linear_size] = GetLinearReadSpace();

    // 绝对位置转相对位置
    start_index %= buffer_.size();
    size_t rel_start = (start_index >= read_index_)
                           ? (start_index - read_index_)
                           : (buffer_.size() - read_index_ + start_index);

    // 分段搜索
    for (size_t i = rel_start; i <= linear_size - key_len; ++i) {
      bool match = true;
      for (size_t j = 0; j < key_len; ++j) {
        if (static_cast<uint8_t>(ptr[i + j]) != find_key[j]) {
          match = false;
          break;
        }
      }
      if (match) {
        return (read_index_ + i) % buffer_.size();
      }
    }

    // 处理跨越缓冲末尾的情况
    if (linear_size < key_len) {
      size_t remaining = key_len - linear_size;
      for (size_t i = 0; i <= buffer_.size() - remaining; ++i) {
        bool match = true;
        for (size_t j = 0; j < key_len; ++j) {
          size_t pos = (read_index_ + linear_size + i + j) % buffer_.size();
          if (static_cast<uint8_t>(buffer_[pos]) != find_key[j]) {
            match = false;
            break;
          }
        }
        if (match) {
          return (read_index_ + linear_size + i) % buffer_.size();
        }
      }
    }

    return buffer_.size(); // 未找到
  }

private:
  HeadSzCb head_type_cb_;
  TailSzCb tail_type_cb_;
  HeadKey head_key_;
  TailKey tail_key_;
};

} // namespace containers