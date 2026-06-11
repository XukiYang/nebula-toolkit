#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace nebula::shmstore {

// ────────────────────────────────────────────────────────────
// 变更事件
// ────────────────────────────────────────────────────────────
enum class Op : uint8_t { Insert = 0x01, Update = 0x02, Erase = 0x03 };

// 轻量级字节视图（C++17 替代 std::span<const uint8_t>）
struct ByteSpan {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct ChangeEvent {
    Op op;
    std::string_view topic;
    ByteSpan key;
};

// ────────────────────────────────────────────────────────────
// 网络包格式：magic(2) ver(1) op(1) seq(4) topic_len(1) topic key_len(1) key crc32(4)
// 总大小 ≤ 128 字节
// ────────────────────────────────────────────────────────────
namespace packet {

constexpr uint16_t kMagic = 0xEB01;
constexpr uint8_t kVersion = 0x01;
constexpr size_t kMinSize = 2 + 1 + 1 + 4 + 1 + 0 + 1 + 0 + 4;  // 空 topic + 空 key = 14
constexpr size_t kMaxSize = 128;
constexpr uint16_t kDefaultPort = 9000;
constexpr uint8_t kMcastPrefix[3] = {239, 0, 0};  // 239.0.0.0/24

// topic → 组播地址最后一段 (1~254)
inline uint8_t topic_to_mcast_last(std::string_view topic) {
    uint32_t h = 5381;
    for (char c : topic) {
        h = ((h << 5) + h) + static_cast<uint8_t>(c);
    }
    return static_cast<uint8_t>(h % 254 + 1);
}

// topic → 组播地址字符串，如 "239.0.0.42"
inline std::string topic_to_mcast(std::string_view topic) {
    auto last = topic_to_mcast_last(topic);
    return std::to_string(kMcastPrefix[0]) + "." + std::to_string(kMcastPrefix[1]) + "." +
           std::to_string(kMcastPrefix[2]) + "." + std::to_string(last);
}

}  // namespace packet

// ────────────────────────────────────────────────────────────
// CRC32（与 include/crypto/crc32.hpp 算法一致，内联一份保持自包含）
// ────────────────────────────────────────────────────────────
namespace detail {

inline uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

inline void put_le32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t get_le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace detail

// ────────────────────────────────────────────────────────────
// 编码：ChangeEvent + seq → 网络包
// ────────────────────────────────────────────────────────────
inline std::vector<uint8_t> encode_packet(const ChangeEvent& ev, uint32_t seq) {
    std::vector<uint8_t> buf;
    buf.reserve(packet::kMaxSize);

    // magic(2) + ver(1) + op(1)
    buf.push_back(0xEB);
    buf.push_back(0x01);
    buf.push_back(packet::kVersion);
    buf.push_back(static_cast<uint8_t>(ev.op));

    // seq (小端)
    uint8_t seq_bytes[4];
    detail::put_le32(seq_bytes, seq);
    buf.insert(buf.end(), seq_bytes, seq_bytes + 4);

    // topic (len + data)
    if (ev.topic.size() > 255) return {};
    buf.push_back(static_cast<uint8_t>(ev.topic.size()));
    buf.insert(buf.end(), ev.topic.begin(), ev.topic.end());

    // key (len + data)
    if (ev.key.size > 255) return {};
    buf.push_back(static_cast<uint8_t>(ev.key.size));
    buf.insert(buf.end(), ev.key.data, ev.key.data + ev.key.size);

    // crc32 覆盖前面所有字节
    uint32_t crc = detail::crc32(buf.data(), buf.size());
    uint8_t crc_bytes[4];
    detail::put_le32(crc_bytes, crc);
    buf.insert(buf.end(), crc_bytes, crc_bytes + 4);

    return buf;
}

// ────────────────────────────────────────────────────────────
// 解码
// ────────────────────────────────────────────────────────────
struct DecodeResult {
    bool ok = false;
    Op op = Op::Insert;
    uint32_t seq = 0;
    std::string_view topic;
    ByteSpan key;
};

// 网络包 → DecodeResult，校验失败返回 ok=false
inline DecodeResult decode_packet(const uint8_t* data, size_t len) {
    DecodeResult r;

    if (len < packet::kMinSize || len > packet::kMaxSize) return r;
    if (data[0] != 0xEB || data[1] != 0x01) return r;

    // CRC 校验
    uint32_t expected_crc = detail::get_le32(data + len - 4);
    uint32_t actual_crc = detail::crc32(data, len - 4);
    if (expected_crc != actual_crc) return r;

    if (data[2] != packet::kVersion) return r;

    r.op = static_cast<Op>(data[3]);
    r.seq = detail::get_le32(data + 4);

    // topic
    uint8_t topic_len = data[8];
    if (9 + topic_len + 1 + 4 > len) return r;  // 至少还能容纳 key_len + crc32
    r.topic = std::string_view(reinterpret_cast<const char*>(data + 9), topic_len);

    // key
    size_t key_offset = 9 + topic_len;
    uint8_t key_len = data[key_offset];
    if (key_offset + 1 + key_len + 4 > len) return r;
    r.key = ByteSpan{data + key_offset + 1, key_len};

    r.ok = true;
    return r;
}

}  // namespace nebula::shmstore
