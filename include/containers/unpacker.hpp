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
  enum UnpackerModel { kNone, kHead, kHeadTail, KHeadTailCb };

public:
  static std::unique_ptr<UnPacker> CreateBasic(HeadKey &&h, TailKey &&t,
                                               size_t s) {

    // 此处直接返回unique_ptr不可行，智能指针模板无权访问内部构造，只能new再转unique_ptr
    return std::unique_ptr<UnPacker>(
        new UnPacker(std::move(h), std::move(t), s));
  }

  static std::unique_ptr<UnPacker> CreateWithCallbacks(HeadKey &&h, TailKey &&t,
                                                       DataSzCb &&dc,
                                                       CheckValidCb &&cc,
                                                       size_t s) {
    return std::unique_ptr<UnPacker>(new UnPacker(
        std::move(h), std::move(t), std::move(dc), std::move(cc), s));
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
    return UnpackerModel::kNone;
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
    if (read_data.size() > 0)
      read_data.clear(); // 清空容器留存包，避免重复处理
    GetPack(read_data);
    return write_size;
  };

  /// @brief 仅解析已有的数据包
  /// @param read_data
  /// @return
  size_t Get(std::vector<std::vector<uint8_t>> &read_data) {
    LOGP_DEBUG("Get Pack,AvailableToRead:%d", AvailableToRead());
    if (read_data.size() > 0)
      read_data.clear(); // 清空容器留存包，避免重复处理
    return GetPack(read_data);
  };

private:
  /// @brief
  /// @param head_key
  /// @param tail_key
  /// @param buffer_size
  UnPacker(HeadKey &&head_key, TailKey &&tail_key, size_t buffer_size = 1024)
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
  UnPacker(HeadKey &&head_key, TailKey &&tail_key, DataSzCb &&data_sz_cb,
           CheckValidCb &&check_sz_cb, size_t buffer_size = 1024)
      : RingBuffer(buffer_size), head_key_(std::move(head_key)),
        data_sz_cb_(std::move(data_sz_cb)),
        check_sz_cb_(std::move(check_sz_cb)), tail_key_(std::move(tail_key)) {
    unpacker_model_ = CheckModel();
    LOGP_DEBUG("unpackermodel:%d", unpacker_model_);
  }

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
  /// @return 找到返回相对位置，未找到返回缓冲区大小
  size_t FindKey(const std::vector<uint8_t> &find_key,
                 size_t start_offset = 0) {
    if (find_key.empty() || find_key.size() > AvailableToRead() ||
        start_offset >= AvailableToRead()) {
      return buffer_.size(); // 无效参数
    }

    const size_t key_len = find_key.size();
    const size_t total_size = AvailableToRead();

    // 计算绝对起始位置（考虑环回）
    const size_t abs_start = (read_index_ + start_offset) % buffer_.size();

    // 可能的两部分（如果环回）
    size_t part1_size =
        std::min(buffer_.size() - abs_start, total_size - start_offset);
    size_t part2_size = total_size - start_offset - part1_size;

    // 检查第一部分
    for (size_t i = 0; i <= part1_size - key_len; ++i) {
      bool match = true;
      for (size_t j = 0; j < key_len; ++j) {
        if (buffer_[(abs_start + i + j) % buffer_.size()] != find_key[j]) {
          match = false;
          break;
        }
      }
      if (match) {
        return start_offset + i; // 返回相对位置
      }
    }

    // 检查第二部分（如果有）
    if (part2_size >= key_len) {
      for (size_t i = 0; i <= part2_size - key_len; ++i) {
        bool match = true;
        for (size_t j = 0; j < key_len; ++j) {
          if (buffer_[i + j] != find_key[j]) {
            match = false;
            break;
          }
        }
        if (match) {
          return start_offset + part1_size + i;
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
    size_t current_pos = 0;
    const size_t total_size = AvailableToRead(); // 保存当前可读数据总量
    while (current_pos < total_size) {
      // 查找头定位符
      size_t head_offset = FindKey(head_key_, current_pos);
      if (head_offset == buffer_.size())
        break;

      // 从头部后开始查找下一个头
      size_t next_head_offset =
          FindKey(head_key_, head_offset + head_key_.size());
      if (next_head_offset == buffer_.size())
        break;

      // 计算包大小（从当前头到下一个头）
      size_t packet_size = next_head_offset - head_offset;

      // 提取数据包
      std::vector<uint8_t> packet(packet_size);
      const size_t abs_head = (read_index_ + head_offset) % buffer_.size();

      // 计算线性拷贝部分
      const size_t to_end = buffer_.size() - abs_head;
      const size_t part1_size = std::min(packet_size, to_end);

      // 拷贝第一部分
      memcpy(packet.data(), buffer_.data() + abs_head, part1_size);

      // 处理环回部分
      if (packet_size > part1_size) {
        memcpy(packet.data() + part1_size, buffer_.data(),
               packet_size - part1_size);
      }

      read_data.push_back(std::move(packet));
      current_pos = head_offset + packet_size; // 移动到当前包结束位置

      // 提交读取
      CommitReadSize(packet_size);
    }
    return UnPackerResult::kSuccess;
  };

  /// @brief 头尾定位符分包模式
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult
  ProcessHeadTailMode(std::vector<std::vector<uint8_t>> &read_data) {
    size_t current_pos = 0;
    const size_t total_size = AvailableToRead(); // 保存当前可读数据总量
    while (current_pos < total_size) {
      // 查找头定位符
      size_t head_offset = FindKey(head_key_, current_pos);
      if (head_offset == buffer_.size())
        break; // 找不到头

      // 从头部后开始查找尾
      size_t tail_offset = FindKey(tail_key_, head_offset + head_key_.size());
      if (tail_offset == buffer_.size())
        break; // 找不到尾

      // 计算包尺寸（包括头尾）
      size_t packet_size = tail_offset + tail_key_.size() - head_offset;

      // 提取数据包
      std::vector<uint8_t> packet(packet_size);
      const size_t abs_head = (read_index_ + head_offset) % buffer_.size();

      // 计算线性复制部分
      const size_t to_end = buffer_.size() - abs_head;
      const size_t part1_size = std::min(packet_size, to_end);

      // 复制第一部分
      memcpy(packet.data(), buffer_.data() + abs_head, part1_size);

      // 如果有环回部分
      if (packet_size > part1_size) {
        memcpy(packet.data() + part1_size, buffer_.data(),
               packet_size - part1_size);
      }

      read_data.push_back(std::move(packet));
      current_pos = tail_offset + tail_key_.size(); // 移动当前位置

      // 提交读取
      CommitReadSize(packet_size);
    }

    return kSuccess;
  }

  /// @brief 头尾定位符以及回调分包模式
  /// @param read_data
  /// @return UnPackerResult
  UnPackerResult
  ProcessHeadTailAndCbMode(std::vector<std::vector<uint8_t>> &read_data) {
    size_t current_pos = 0;
    const size_t total_size = AvailableToRead();

    while (current_pos < total_size) {
      // 查找头定位符
      size_t head_offset = FindKey(head_key_, current_pos);
      if (head_offset == buffer_.size())
        break;

      // 应用回调获取包结构
      const size_t abs_head = (read_index_ + head_offset) % buffer_.size();
      size_t head_size = 0, data_size = 0, tail_size = 0;
      data_sz_cb_(buffer_.data() + abs_head, head_size, data_size, tail_size);

      size_t packet_size = head_size + data_size + tail_size;

      // 验证包尺寸合理性
      if (head_size < head_key_.size() || tail_size < tail_key_.size() ||
          head_offset + packet_size > total_size) {
        current_pos = head_offset + 1; // 移动到下一个字节
        continue;
      }

      // 验证尾定位符位置
      size_t expected_tail_offset = head_offset + head_size + data_size;
      size_t tail_offset = FindKey(tail_key_, expected_tail_offset);

      // 尾定位符不匹配
      if (tail_offset != expected_tail_offset) {
        current_pos = head_offset + 1;
        continue;
      }

      // 创建完整包
      std::vector<uint8_t> packet(packet_size);
      const size_t to_end = buffer_.size() - abs_head;
      const size_t part1_size = std::min(packet_size, to_end);

      memcpy(packet.data(), buffer_.data() + abs_head, part1_size);

      if (packet_size > part1_size) {
        memcpy(packet.data() + part1_size, buffer_.data(),
               packet_size - part1_size);
      }

      // 应用校验
      if (!check_sz_cb_ || check_sz_cb_(packet.data())) {
        read_data.push_back(std::move(packet));
        current_pos = head_offset + packet_size;
        CommitReadSize(packet_size);
      } else {
        current_pos = head_offset + 1; // 校验失败，移动到下一个字节
      }
    }

    return kSuccess;
  }

private:
  HeadKey head_key_{};
  TailKey tail_key_{};

  DataSzCb data_sz_cb_ = nullptr;
  CheckValidCb check_sz_cb_ = nullptr;
  UnpackerModel unpacker_model_ = UnpackerModel::kNone;
};

} // namespace containers