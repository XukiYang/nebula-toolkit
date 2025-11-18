#pragma once
#include <cstring>
#include <iostream>
#include <vector>

namespace containers {
constexpr size_t BytesStreamSize = 1024;
class BytesStream {
private:
    std::vector<char> buffer_;
    size_t            write_pos_;
    size_t            read_pos_;

private:
    BytesStream& operator=(const BytesStream&) = delete;
    BytesStream(const BytesStream&)            = delete;

public:
    explicit BytesStream(size_t buffer_size = BytesStreamSize) : buffer_(buffer_size), write_pos_(0), read_pos_(0) {}
    ~BytesStream() = default;
    /* 自定义类型序列化，仅限结构体 */
    template <typename T>
    BytesStream& operator<<(const T& data) {
        size_t t_size = sizeof(T);
        if (write_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Write overflow in BytesStream");
        }
        std::memcpy(buffer_.data() + write_pos_, &data, t_size);
        write_pos_ += t_size;
        return *this;
    }
    template <typename T>
    BytesStream& operator>>(T& data) {
        size_t t_size = sizeof(T);
        if (read_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Read overflow in BytesStream");
        }
        std::memcpy(&data, buffer_.data() + read_pos_, t_size);
        read_pos_ += t_size;
        return *this;
    }

    /* Vector类型序列化 */
    template <typename T>
    BytesStream& operator<<(const std::vector<T>& data) {
        size_t t_size = data.size() * sizeof(T);
        if (write_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Write overflow in BytesStream");
        }
        std::memcpy(buffer_.data() + write_pos_, &data, t_size);
        write_pos_ += t_size;
        return *this;
    }
    template <typename T>
    BytesStream& operator<<(std::vector<T>& data) {
        size_t t_size = data.size() * sizeof(T);
        if (read_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Read overflow in BytesStream");
        }
        std::memcpy(&data, buffer_.data() + read_pos_, t_size);
        read_pos_ += t_size;
        return *this;
    }

    /* String类型序列化 */
    BytesStream& operator<<(const std::string& data) {
        size_t t_size = data.size();
        if (write_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Write overflow in BytesStream");
        }
        std::memcpy(buffer_.data() + write_pos_, &data, t_size);
        write_pos_ += t_size;
        return *this;
    }
    BytesStream& operator<<(std::string& data) {
        size_t t_size = data.size();
        if (read_pos_ + t_size > buffer_.size()) {
            throw std::runtime_error("Read overflow in BytesStream");
        }
        std::memcpy(&data, buffer_.data() + read_pos_, t_size);
        read_pos_ += t_size;
        return *this;
    }

    /* 获取已写字节数 */
    size_t Size() const {
        return write_pos_;
    }
    /* 获取Raw数据起始指针 */
    const char* Data() const {
        return buffer_.data();
    }
    /* 清空 */
    void Clear() {
        read_pos_  = 0;
        write_pos_ = 0;
    }
};
}  // namespace containers
