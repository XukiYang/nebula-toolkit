#pragma once
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <functional>
#include <memory>

#include "containers/unpacker.hpp"
#include "logger/logger.hpp"
#include "net/transport/enums.hpp"

namespace nebula {
namespace net {
namespace transport {

/// @brief 业务执行回调类型定义
/// @param packs 解析后的数据包
using ExecCb = std::function<void(int fd, const std::vector<std::vector<uint8_t>> &packs)>;

/// @brief 协议处理器基类
/// 处理不同协议的事件，提供统一接口
/// 处理器可以是TCP、UDP等协议的具体实现
/// 通过继承此类实现具体协议的处理逻辑
class ProtocolHandler {
public:
    virtual void HandleEvent(const EventContext &ctx) = 0;
    virtual bool ShouldClose() const {
        return false;
    }
    virtual ~ProtocolHandler() = default;
};

/// @brief TCP协议处理器
class TcpHandler : public ProtocolHandler {
public:
    TcpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
        : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false), is_closed_(false) {}

    void SetCallback(ExecCb cb) {
        cb_ = std::move(cb);
    }
    bool ShouldClose() const override {
        return should_close_ || is_closed_;
    }

    void HandleEvent(const EventContext &ctx) override {
        if (ctx.fd != fd_) return;

        // 处理错误事件
        if (ctx.event_flags & EventFlags::kError) {
            LOGP_MSG("Connection error on fd:%d", fd_);
            should_close_ = true;
            return;
        }

        // 处理连接挂起
        if (ctx.event_flags & EventFlags::kHangUp) {
            LOGP_MSG("Connection closed by peer on fd:%d", fd_);
            should_close_ = true;
            return;
        }

        // 处理可读事件（边缘触发模式）
        if (ctx.event_flags & EventFlags::kReadable) {
            if (!should_close_) {
                ProcessReadableEvent(ctx);
            }
        }
    }

    ~TcpHandler() override {
        is_closed_ = true;  // 在析构时设置标志
    }

private:
    const int                             fd_;
    bool                                  should_close_;
    bool                                  is_closed_;
    ExecCb                                cb_;
    std::unique_ptr<containers::UnPacker> unpacker_;
    std::vector<std::vector<uint8_t>>     packs_;

    void ProcessReadableEvent(const EventContext &ctx) {
        while (true) {
            auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
            if (capacity == 0) {
                LOGP_MSG("Buffer full on fd:%d,wirte space:%d,read space:%d", fd_, unpacker_->AvailableToWrite(),
                         unpacker_->AvailableToRead());
                break;
            }

            ssize_t n = read(fd_, buffer, capacity);

            if (n > 0) {
                // 提交写入数据
                unpacker_->CommitWriteSize(n);

                // 解析数据包
                unpacker_->Get(packs_);

                if (!packs_.empty() && cb_ && !should_close_) {
                    auto local_packs = std::move(packs_);
                    packs_.clear();
                    // 这里只做“把已解包结果交给业务层”，不直接在这里回包。
                    // 业务层通常会生成Frame并投递给Reactor的响应队列。
                    auto timer_task = [fd = fd_, cb = cb_, packs = std::move(local_packs)]() mutable {
                        cb(fd, packs);
                        return 0;
                    };
                    if (ctx.timer_scheduler) {
                        ctx.timer_scheduler->ScheduleOnce(0, timer_task);
                    } else {
                        timer_task();
                    }
                }
            } else if (n == 0) {  // 对端关闭连接
                should_close_ = true;
                break;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 没有更多数据可读
            } else {
                perror("read");
                should_close_ = true;
                break;
            }
        }
    }
};

class UdpHandler : public ProtocolHandler {
public:
    UdpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
        : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false) {}

    void SetCallback(ExecCb cb) {
        cb_ = std::move(cb);
    }

    void HandleEvent(const EventContext &ctx) override {
        if (ctx.event_flags & EventFlags::kError) {
            should_close_ = true;
            return;
        }
        if (ctx.event_flags & EventFlags::kReadable) {
            while (true) {
                auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
                if (capacity == 0) {
                    LOGP_MSG("Buffer full on fd:%d,wirte space:%d,read space:%d", fd_, unpacker_->AvailableToWrite(),
                             unpacker_->AvailableToRead());
                    break;
                }
                struct sockaddr_storage addr;
                socklen_t               addr_len = sizeof(addr);
                ssize_t len = recvfrom(fd_, buffer, capacity, 0, reinterpret_cast<sockaddr *>(&addr), &addr_len);

                if (len == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;  // 读取完毕
                    }
                    // 发生错误
                    LOGP_ERROR("udp error on fd:%d,errno:%d", fd_, errno);
                    should_close_ = true;
                    break;  // 必须 break，否则 len==-1 会落入 CommitWriteSize 导致越界
                }

                unpacker_->CommitWriteSize(len);
                unpacker_->Get(packs_);

                if (!packs_.empty() && cb_) {
                    auto local_packs = std::move(packs_);
                    packs_.clear();
                    // UDP路径与TCP一致：回调只产出业务结果，不直接操作socket发送。
                    auto timer_task = [fd = fd_, cb = cb_, packs = std::move(local_packs)]() mutable {
                        cb(fd, packs);
                        return 0;
                    };
                    if (ctx.timer_scheduler) {
                        ctx.timer_scheduler->ScheduleOnce(0, timer_task);
                    } else {
                        timer_task();
                    }
                }
            }
        }
    };
    bool ShouldClose() const override {
        return should_close_;
    }

private:
    const int                             fd_;
    bool                                  should_close_;
    ExecCb                                cb_;
    std::unique_ptr<containers::UnPacker> unpacker_;
    std::vector<std::vector<uint8_t>>     packs_;
};

}  // namespace transport
}  // namespace net
}  // namespace nebula
