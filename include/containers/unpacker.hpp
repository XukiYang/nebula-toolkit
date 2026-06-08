#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "containers/circular_buffer.hpp"

#ifndef LOGP_DEBUG
#define LOGP_DEBUG(...)
#endif

#ifndef LOG_DEBUG
#define LOG_DEBUG(...)
#endif

namespace nebula {
namespace containers {

using DataSzCb = std::function<void(const uint8_t* head_ptr, size_t& head_size, size_t& data_size, size_t& tail_size)>;
using CheckValidCb = std::function<bool(const uint8_t* data_ptr)>;

using HeadKey = std::vector<uint8_t>;
using TailKey = std::vector<uint8_t>;

class UnPacker : public CircularBuffer {
    enum class Result { kSuccess = 0, kError = -1, kNone };
    enum class Mode { kNone, kHead, kHeadTail, kHeadTailCb };
    enum class IncludeMode { kNone, kInclude };

private:
    HeadKey head_key_{};
    TailKey tail_key_{};
    DataSzCb data_sz_cb_ = nullptr;
    CheckValidCb check_sz_cb_ = nullptr;
    Mode unpacker_model_ = Mode::kNone;
    IncludeMode unpacker_include_ = IncludeMode::kNone;

public:
    static std::unique_ptr<UnPacker> CreateHeadOnly(HeadKey head_key, uint32_t buffer_size = 4096,
                                                    IncludeMode include_mode = IncludeMode::kNone) {
        return std::unique_ptr<UnPacker>(new UnPacker(std::move(head_key), TailKey{}, buffer_size, include_mode));
    }

    static std::unique_ptr<UnPacker> CreateHeadTail(HeadKey head_key, TailKey tail_key, uint32_t buffer_size = 4096,
                                                    IncludeMode include_mode = IncludeMode::kNone) {
        return std::unique_ptr<UnPacker>(
            new UnPacker(std::move(head_key), std::move(tail_key), buffer_size, include_mode));
    }

    static std::unique_ptr<UnPacker> CreateWithCallbacks(HeadKey head_key, TailKey tail_key, DataSzCb data_sz_cb,
                                                         CheckValidCb check_valid_cb, uint32_t buffer_size = 4096,
                                                         IncludeMode include_mode = IncludeMode::kNone) {
        return std::unique_ptr<UnPacker>(new UnPacker(std::move(head_key), std::move(tail_key), std::move(data_sz_cb),
                                                      std::move(check_valid_cb), buffer_size, include_mode));
    }

    size_t PushAndGet(const uint8_t* write_data, size_t data_size, std::vector<std::vector<uint8_t>>& read_data) {
        if (write_data == nullptr || data_size == 0) {
            return 0;
        }
        const size_t write_size = Write(write_data, data_size);
        LOGP_DEBUG("write_ret:%zu,AvailableToRead:%zu", write_size, AvailableToRead());
        if (!read_data.empty()) {
            read_data.clear();
        }
        GetPack(read_data);
        return write_size;
    }

    size_t Get(std::vector<std::vector<uint8_t>>& read_data) {
        LOGP_DEBUG("Get Pack,AvailableToRead:%zu", AvailableToRead());
        if (!read_data.empty()) {
            read_data.clear();
        }
        GetPack(read_data);
        return read_data.size();
    }

private:
    UnPacker(HeadKey&& head_key, TailKey&& tail_key, uint32_t buffer_size, IncludeMode include_mode)
        : CircularBuffer(buffer_size),
          head_key_(std::move(head_key)),
          tail_key_(std::move(tail_key)),
          unpacker_include_(include_mode) {
        unpacker_model_ = CheckModel();
    }

    UnPacker(HeadKey&& head_key, TailKey&& tail_key, DataSzCb&& data_sz_cb, CheckValidCb&& check_valid_cb,
             uint32_t buffer_size, IncludeMode include_mode)
        : CircularBuffer(buffer_size),
          head_key_(std::move(head_key)),
          tail_key_(std::move(tail_key)),
          data_sz_cb_(std::move(data_sz_cb)),
          check_sz_cb_(std::move(check_valid_cb)),
          unpacker_include_(include_mode) {
        unpacker_model_ = CheckModel();
    }

    Mode CheckModel() const {
        if (data_sz_cb_ && check_sz_cb_ && !head_key_.empty() && !tail_key_.empty()) return Mode::kHeadTailCb;
        if (!head_key_.empty() && tail_key_.empty()) return Mode::kHead;
        if (!head_key_.empty() && !tail_key_.empty()) return Mode::kHeadTail;
        return Mode::kNone;
    }

    static size_t FindKeyLinear(const std::vector<uint8_t>& data, const std::vector<uint8_t>& key, size_t start = 0) {
        if (key.empty() || start >= data.size() || key.size() > data.size()) {
            return data.size();
        }
        for (size_t i = start; i + key.size() <= data.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < key.size(); ++j) {
                if (data[i + j] != key[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return i;
        }
        return data.size();
    }

    Result GetPack(std::vector<std::vector<uint8_t>>& read_data) {
        switch (unpacker_model_) {
        case Mode::kHead:
            return ProcessHeadOnlyMode(read_data);
        case Mode::kHeadTail:
            return ProcessHeadTailMode(read_data);
        case Mode::kHeadTailCb:
            return ProcessHeadTailAndCbMode(read_data);
        default:
            LOG_DEBUG("unknown unpack mode");
            return Result::kError;
        }
    }

    Result ProcessHeadOnlyMode(std::vector<std::vector<uint8_t>>& read_data) {
        while (true) {
            const size_t available = AvailableToRead();
            if (available == 0) break;

            std::vector<uint8_t> buffer(available);
            Peek(buffer, available);

            const size_t head_offset = FindKeyLinear(buffer, head_key_, 0);
            if (head_offset == buffer.size()) {
                CommitReadSize(available);
                break;
            }

            const size_t next_head_offset = FindKeyLinear(buffer, head_key_, head_offset + head_key_.size());
            if (next_head_offset == buffer.size()) {
                if (head_offset > 0) CommitReadSize(head_offset);
                break;
            }

            const size_t packet_size = next_head_offset - head_offset;
            const size_t consume_size = head_offset + packet_size;
            if (unpacker_include_ == IncludeMode::kInclude) {
                read_data.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(head_offset),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(head_offset + packet_size));
            } else {
                const size_t payload_begin = head_offset + head_key_.size();
                read_data.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(payload_begin),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(head_offset + packet_size));
            }
            CommitReadSize(consume_size);
        }
        return Result::kSuccess;
    }

    Result ProcessHeadTailMode(std::vector<std::vector<uint8_t>>& read_data) {
        while (true) {
            const size_t available = AvailableToRead();
            if (available == 0) break;

            std::vector<uint8_t> buffer(available);
            Peek(buffer, available);

            const size_t head_offset = FindKeyLinear(buffer, head_key_, 0);
            if (head_offset == buffer.size()) {
                CommitReadSize(available);
                break;
            }

            const size_t tail_offset = FindKeyLinear(buffer, tail_key_, head_offset + head_key_.size());
            if (tail_offset == buffer.size()) {
                if (head_offset > 0) CommitReadSize(head_offset);
                break;
            }

            const size_t packet_size = tail_offset + tail_key_.size() - head_offset;
            const size_t consume_size = head_offset + packet_size;
            if (unpacker_include_ == IncludeMode::kInclude) {
                read_data.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(head_offset),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(head_offset + packet_size));
            } else {
                const size_t payload_begin = head_offset + head_key_.size();
                const size_t payload_end = tail_offset;
                read_data.emplace_back(buffer.begin() + static_cast<std::ptrdiff_t>(payload_begin),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(payload_end));
            }
            CommitReadSize(consume_size);
        }
        return Result::kSuccess;
    }

    Result ProcessHeadTailAndCbMode(std::vector<std::vector<uint8_t>>& read_data) {
        while (true) {
            const size_t available = AvailableToRead();
            if (available == 0) break;

            std::vector<uint8_t> buffer(available);
            Peek(buffer, available);

            const size_t head_offset = FindKeyLinear(buffer, head_key_, 0);
            if (head_offset == buffer.size()) {
                CommitReadSize(available);
                break;
            }

            size_t head_size = 0;
            size_t data_size = 0;
            size_t tail_size = 0;
            data_sz_cb_(buffer.data() + head_offset, head_size, data_size, tail_size);
            const size_t packet_size = head_size + data_size + tail_size;
            if (packet_size == 0 || head_offset + packet_size > buffer.size()) {
                if (head_offset > 0) CommitReadSize(head_offset);
                break;
            }

            const size_t expected_tail_offset = head_offset + head_size + data_size;
            const size_t tail_offset = FindKeyLinear(buffer, tail_key_, expected_tail_offset);
            if (tail_offset != expected_tail_offset) {
                CommitReadSize(head_offset + 1);
                continue;
            }

            std::vector<uint8_t> packet(buffer.begin() + static_cast<std::ptrdiff_t>(head_offset),
                                        buffer.begin() + static_cast<std::ptrdiff_t>(head_offset + packet_size));
            if (!check_sz_cb_ || check_sz_cb_(packet.data())) {
                read_data.push_back(std::move(packet));
                CommitReadSize(head_offset + packet_size);
            } else {
                CommitReadSize(head_offset + 1);
            }
        }
        return Result::kSuccess;
    }
};

}  // namespace containers
}  // namespace nebula
