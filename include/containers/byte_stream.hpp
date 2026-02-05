#pragma once
#include <string.h>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "../logger/logger.hpp"
#include "circular_buffer.hpp"
namespace nebula {

namespace containers {

/// @brief 字节序列化工具
/// @warning 必须1字节对齐
class ByteStream : public CircularBuffer {
public:
    /// @brief 转发构造函数​​
    /// @param buffer_size
    ByteStream(size_t buffer_size = 128) : CircularBuffer(buffer_size) {}

    /// @brief 读取自定义类型数据
    /// @tparam T
    /// @param data
    /// @return
    template <typename T>
    ByteStream &operator>>(T &data) {
        Read(reinterpret_cast<std::byte *>(&data), sizeof(T));
        return *this;
    };

    /// @brief 写入自定义类型数据
    /// @tparam T
    /// @param data
    /// @return
    template <typename T>
    ByteStream &operator<<(const T &data) {
        Write(reinterpret_cast<const std::byte *>(&data), sizeof(T));
        return *this;
    };

    /// @brief 写入std::vector<T>数据
    /// @tparam T
    /// @param data
    /// @return
    template <typename T>
    ByteStream &operator<<(const std::vector<T> &data) {
        Write(data);
        return *this;
    };

    /// @brief 读取std::vector<T>数据
    /// @note data的字节数必须<=环形缓冲区可读空间，否则不会读出数据
    /// @tparam T
    /// @param data
    /// @return
    template <typename T>
    ByteStream &operator>>(std::vector<T> &data) {
        Read(data, data.size() * sizeof(T));
        return *this;
    };

    /// @brief 写入std::string数据
    /// @param data
    /// @return
    ByteStream &operator<<(const std::string &data) {
        Write(reinterpret_cast<const std::byte *>(data.data()), data.size());
        return *this;
    }

    /// @brief 读取std::string数据
    /// @note data的字节数必须<=环形缓冲区可读空间，否则不会读出数据
    /// @param data
    /// @return
    ByteStream &operator>>(std::string &data) {
        Read(reinterpret_cast<std::byte *>(data.data()), data.size());
        return *this;
    };
};
}  // namespace containers
}  // namespace nebula