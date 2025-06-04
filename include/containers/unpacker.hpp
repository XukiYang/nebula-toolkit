#pragma once
#include "../logkit/logkit.hpp"
#include "ring_buffer.hpp"
#include <functional>

namespace containers {

/** 解析字节数回调:外部定义自定义解析逻辑
 * *head_ptr:解析数据头指针
 * &head_size:获取头大小
 * &data_size:获取头尾之外数据大小
 * &tail_size:获取尾数据大小
 */
using DataSzCb = std::function<void(const uint8_t *head_ptr, size_t &head_size,
                                    size_t &data_size, size_t &tail_size)>;
/** 校验检查回调:外部传入自定义校验逻辑
 * *data_ptr:校验数据指针
 * &data_len:获取校验数据长度
 * bool ret:返回是否有效或无效
 */
using CheckValidCb = std::function<bool(const uint8_t *data_ptr)>;

// 头定位符
using HeadKey = std::vector<uint8_t>;
// 尾定位符
using TailKey = std::vector<uint8_t>;

class UnPacker : public RingBuffer {
  enum UnPackerResult { kSuccess = 0, kError = -1 };
  enum UnpackerModel { kHead, kHeadTail, KHeadTailCb };

public:
  /// @brief
  /// @param head_key
  /// @param tail_key
  /// @param buffer_size
  UnPacker(HeadKey &&head_key = {}, TailKey &&tail_key = {},
           size_t buffer_size = 1024)
      : RingBuffer(buffer_size), head_key_(std::move(head_key)),
        tail_key_(std::move(tail_key)) {
    unpacker_model_ = CheckModel();
    LOGP_DEBUG("unpackermodel:%d", unpacker_model_);
  }

  /// @brief 基于单头定位符与头尾定位符以及回调的构造
  /// @param head_key
  /// @param tail_key
  /// @param data_sz_cb
  /// @param check_sz_cb
  /// @param buffer_size
  UnPacker(HeadKey &&head_key = {}, TailKey &&tail_key = {},
           DataSzCb &&data_sz_cb = nullptr,
           CheckValidCb &&check_sz_cb = nullptr, size_t buffer_size = 1024)
      : RingBuffer(buffer_size), head_key_(std::move(head_key)),
        data_sz_cb_(std::move(data_sz_cb)),
        check_sz_cb_(std::move(check_sz_cb)), tail_key_(std::move(tail_key)) {
    unpacker_model_ = CheckModel();
    LOGP_DEBUG("unpackermodel:%d", unpacker_model_);
  }

  /// @brief 检查解包模式
  /// @return UnpackerModel
  UnpackerModel CheckModel() {
    if (data_sz_cb_ && check_sz_cb_ && !head_key_.empty() && !tail_key_.empty())
      return UnpackerModel::KHeadTailCb;
    else if (!head_key_.empty() && tail_key_.empty())
      return UnpackerModel::kHead;
    else if (!head_key_.empty() && !tail_key_.empty())
      return UnpackerModel::kHeadTail;
  };

  /// @brief 提交数据并解析数据包
  /// @param write_data
  /// @param data_size
  /// @param read_data
  /// @return 提交的数据大小
  size_t PushAndGet(const uint8_t *write_data, size_t data_size,
                    std::vector<std::vector<uint8_t>> &read_data) {
    if (write_data == nullptr) {
      return UnPackerResult::kError;
    }
    size_t write_size =
        Write(reinterpret_cast<const std::byte *>(write_data), data_size);
    LOGP_DEBUG("write_ret:%d,AvailableToRead:%d", write_size,
               AvailableToRead());
    GetPack(read_data);
    return write_size;
  };

  /// @brief 仅解析已有的数据包
  /// @param read_data
  /// @return
  size_t Get(std::vector<std::vector<uint8_t>> &read_data) {
    LOGP_DEBUG("Get Pack,AvailableToRead:%d", AvailableToRead());
    GetPack(read_data);
  };

private:
  /// @brief 解析数据包到引用
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult GetPack(std::vector<std::vector<uint8_t>> &read_data) {

    // 仅头定位符（包含头定位符）
    if (unpacker_model_ == UnpackerModel::kHead) {
      return ProcessHeadOnlyMode(read_data);
    }
    // 头和尾定位符（包含头尾定位符）
    else if (unpacker_model_ == UnpackerModel::kHeadTail) {
      return ProcessHeadTailMode(read_data);
    }
    // 头尾加字节数与校验回调模式
    else if (unpacker_model_ == UnpackerModel::KHeadTailCb) {
      return ProcessHeadTailAndCbMode(read_data);
    }
    // 其他
    else {
      LOG_DEBUG("未知模式");
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
  /// @brief 仅头定位符分包模式
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult
  ProcessHeadOnlyMode(std::vector<std::vector<uint8_t>> &read_data) {
    // 查找头定位符
    size_t first_head_pos = FindKey(head_key_, history_find_key_pos_);
    LOGP_DEBUG("head_pos:%d,history_find_key_pos:%d", first_head_pos,
               history_find_key_pos_);

    if (first_head_pos == buffer_.size()) {
      LOGP_DEBUG("not found first pack head,pos:%d", first_head_pos);
      return UnPackerResult::kError;
    }
    while (AvailableToRead()) {
      // 查找下一个头定位符
      size_t second_head_pos =
          FindKey(head_key_, first_head_pos + head_key_.size());

      history_find_key_pos_ = first_head_pos; // 记录上一个头位置

      // 下一个头定位符到达缓冲区末尾
      if (second_head_pos == buffer_.size()) {
        LOGP_DEBUG("not found next pack head,pos:%d", first_head_pos);
        return UnPackerResult::kError;
      }

      // 计算包含定位符的完整一包数据
      size_t data_len = second_head_pos - first_head_pos;
      std::vector<uint8_t> packet(data_len);

      // 计算线性可读空间
      size_t first_chunk = std::min(data_len, buffer_.size() - first_head_pos);
      memcpy(packet.data(), buffer_.data() + first_head_pos, first_chunk);

      // 如果包数据超过了线性可读空间，则发生环回
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
  };

  /// @brief 头尾定位符分包模式
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult
  ProcessHeadTailMode(std::vector<std::vector<uint8_t>> &read_data) {
    // 查找头定位符
    size_t head_pos = FindKey(head_key_, history_find_key_pos_);
    LOGP_DEBUG("head_pos:%d,history_find_key_pos:%d", head_pos,
               history_find_key_pos_);
    if (head_pos == buffer_.size()) {
      return UnPackerResult::kError;
    }
    while (AvailableToRead()) {
      // 基于头定位符位置查找尾定位符
      size_t tail_pos = FindKey(tail_key_, head_pos + head_key_.size());

      history_find_key_pos_ = tail_pos + tail_key_.size();

      // 未查找到
      if (tail_pos == buffer_.size()) {
        return UnPackerResult::kError;
      }

      // 计算包含定位符的完整一包数据
      size_t data_len = tail_pos - head_pos + tail_key_.size();
      std::vector<uint8_t> packet(data_len);

      // 计算线性可读空间
      size_t first_chunk = std::min(data_len, buffer_.size() - tail_pos);
      memcpy(packet.data(), buffer_.data() + head_pos, first_chunk);

      // 如果包数据超过了线性可读空间，则发生环回
      if (data_len > first_chunk) {
        // 拷贝环回后的剩余数据
        memcpy(packet.data() + first_chunk, buffer_.data(),
               data_len - first_chunk);
      }

      read_data.emplace_back(std::move(packet));
      CommitReadSize(data_len);
      LOGP_DEBUG("head_pos:%d,tail_pos:%d,data_len:%d,first_chunk:%d", head_pos,
                 tail_pos, data_len, first_chunk);

      // 基于尾定位符位置，查找下一包的头定位符位置
      size_t head_pos_t = FindKey(head_key_, tail_pos + tail_key_.size());
      if (head_pos_t == buffer_.size()) {
        LOGP_DEBUG("not found next pack,pos:%d", head_pos_t);
        return UnPackerResult::kError;
      }
      head_pos = head_pos_t;
    }
    return UnPackerResult::kSuccess;
  };

  /// @brief 头尾定位符以及回调分包模式
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult
  ProcessHeadTailAndCbMode(std::vector<std::vector<uint8_t>> &read_data) {
    // 查找头定位符
    size_t head_pos = FindKey(head_key_, history_find_key_pos_);
    LOGP_DEBUG("head_pos:%d,history_find_key_pos:%d", head_pos,
               history_find_key_pos_);
    if (head_pos == buffer_.size()) {
      return UnPackerResult::kError;
    }
    while (AvailableToRead()) {
      size_t head_size = 0, data_size = 0, tail_size = 0;
      data_sz_cb_(buffer_.data() + head_pos, head_size, data_size, tail_size);
      LOGP_DEBUG("head_size:%d,data_size:%d,tail_size:%d", head_size, data_size,
                 tail_size);
      size_t data_len = head_size + data_size + tail_size;

      // 若解出来的完整包长小于头尾定位符，则跳出此包
      if (data_len < head_key_.size() + tail_key_.size()) {
        LOG_DEBUG("pack small");
        continue;
      }
      // 基于头位置，查找包尾，判定是否符合预期
      size_t tail_pos = FindKey(tail_key_, head_pos + head_key_.size());

      if (tail_pos + tail_size - head_pos != data_len) {
        LOGP_DEBUG("no equal,[data_len:%d,%d]head_pos:%d,tail_pos:%d", data_len,
                   tail_pos + tail_size - head_pos, head_pos, tail_pos);
        continue;
      }
      history_find_key_pos_ = tail_pos + tail_key_.size();
      std::vector<uint8_t> packet(data_len);

      // 计算线性可读空间
      size_t first_chunk = std::min(data_len, buffer_.size() - tail_pos);
      memcpy(packet.data(), buffer_.data() + head_pos, first_chunk);

      // 如果包数据超过了线性可读空间，则发生环回
      if (data_len > first_chunk) {
        // 拷贝环回后的剩余数据
        memcpy(packet.data() + first_chunk, buffer_.data(),
               data_len - first_chunk);
      }

      // 数据校验
      bool check_ret = check_sz_cb_(packet.data());

      read_data.emplace_back(std::move(packet));
      CommitReadSize(data_len);
      LOGP_DEBUG("head_pos:%d,tail_pos:%d,[data_len:%d,%d],first_chunk:%d",
                 head_pos, tail_pos, data_len, tail_pos + tail_size - head_pos,
                 first_chunk);

      // 基于尾定位符位置，查找下一包的头定位符位置
      size_t head_pos_t = FindKey(head_key_, tail_pos + tail_key_.size());
      if (head_pos_t == buffer_.size()) {
        LOGP_DEBUG("not found next pack,pos:%d", head_pos_t);
        return UnPackerResult::kError;
      }
      head_pos = head_pos_t;
    }
  }

private:
  HeadKey head_key_;
  TailKey tail_key_;
  size_t history_find_key_pos_ = 0;

  DataSzCb data_sz_cb_;
  CheckValidCb check_sz_cb_;
  UnpackerModel unpacker_model_;
};

} // namespace containers