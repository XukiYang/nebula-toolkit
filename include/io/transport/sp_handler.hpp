#pragma once
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "containers/unpacker.hpp"
#include "logger/logger.hpp"
#include "io/transport/enums.hpp"
#include "io/transport/protocol_handler.hpp"

namespace nebula {
namespace io {
namespace transport {

/// @brief 串口协议处理器
/// 通过 epoll 监听串口 fd 的可读事件，读取数据并通过 UnPacker 解帧。
/// 串口使用水平触发模式（level-triggered），因为串口数据到达的边界
/// 不像 TCP 那样明确，边缘触发可能导致数据丢失。
class SpHandler : public ProtocolHandler {
public:
    /// @brief 构造串口处理器
    /// @param fd        串口文件描述符
    /// @param unpacker  解包器（所有权转移）
    SpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
        : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false) {}

    /// @brief 设置业务执行回调
    void SetCallback(ExecCb cb) {
        cb_ = std::move(cb);
    }

    bool ShouldClose() const override {
        return should_close_;
    }

    int Fd() const override {
        return fd_;
    }

    /// @brief 线程安全的原始数据写入
    /// 覆写基类默认实现，加锁保护并发写入。
    /// HandleResponseFrame（reactor 线程）和业务线程都可能调用此方法。
    int WriteRaw(const void *data, size_t len) override {
        std::lock_guard<std::mutex> lock(write_mutex_);
        ssize_t written = ::write(fd_, data, len);
        if (written < 0) {
            LOGP_MSG("Serial write failed on fd:%d, errno:%d", fd_, errno);
            return -1;
        }
        return static_cast<int>(written);
    }

    /// @brief 处理 epoll 事件
    /// 串口只关心可读事件和错误事件，不处理可写事件（串口发送直接 write）
    void HandleEvent(const EventContext &ctx) override {
        if (ctx.fd != fd_) return;

        // 错误事件：标记关闭
        if (ctx.event_flags & EventFlags::kError) {
            LOGP_MSG("Serial port error on fd:%d", fd_);
            should_close_ = true;
            return;
        }

        // 可读事件：循环读取数据直到 EAGAIN
        if (ctx.event_flags & EventFlags::kReadable) {
            if (!should_close_) {
                ProcessReadableEvent(ctx);
            }
        }
    }

    /// @brief 串口发送（直接 write，无写缓冲）
    /// @param data 数据指针
    /// @param len  数据长度
    /// @return 成功写入的字节数，失败返回 -1
    int Send(const uint8_t *data, size_t len) {
        std::lock_guard<std::mutex> lock(write_mutex_);
        ssize_t written = write(fd_, data, len);
        if (written < 0) {
            LOGP_MSG("Serial write failed on fd:%d, errno:%d", fd_, errno);
            return -1;
        }
        return static_cast<int>(written);
    }

    ~SpHandler() override = default;

private:
    const int                             fd_;
    bool                                  should_close_;
    ExecCb                                cb_;
    std::unique_ptr<containers::UnPacker> unpacker_;
    std::vector<std::vector<uint8_t>>     packs_;
    std::mutex                            write_mutex_;  // 保护 Send() 的并发安全

    /// @brief 处理可读事件：循环读取串口数据并通过解包器解析
    /// 串口使用水平触发模式，必须循环读取直到 EAGAIN，否则下次 epoll_wait
    /// 可能不会再次触发（数据仍在缓冲区中）。
    void ProcessReadableEvent(const EventContext &ctx) {
        while (true) {
            // 获取解包器的线性写入空间
            auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
            if (capacity == 0) {
                LOGP_MSG("Buffer full on serial fd:%d", fd_);
                break;
            }

            // 从串口读取数据
            ssize_t n = read(fd_, buffer, capacity);
            if (n > 0) {
                // 提交写入大小，触发解包
                unpacker_->CommitWriteSize(n);
                unpacker_->Get(packs_);

                // 有完整数据包时，分发给业务回调
                if (!packs_.empty() && cb_ && !should_close_) {
                    auto local_packs = std::move(packs_);
                    packs_.clear();

                    // 通过定时器调度回调，避免在 epoll 线程中执行耗时业务逻辑
                    auto timer_task = [fd = fd_, cb = cb_, packs = std::move(local_packs)]() mutable {
                        cb(fd, packs);
                        return 0;
                    };
                    if (ctx.timer_scheduler) {
                        ctx.timer_scheduler->ScheduleOnce(0, timer_task);
                    } else {
                        // 无定时器时直接在当前线程执行
                        timer_task();
                    }
                }
            } else if (n == 0) {
                // 串口一般不会返回 0（EOF），但安全处理
                break;
            } else {
                // read 返回负值
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 无更多数据
                LOGP_MSG("Serial read error on fd:%d, errno:%d", fd_, errno);
                should_close_ = true;
                break;
            }
        }
    }
};

}  // namespace transport
}  // namespace io
}  // namespace nebula
