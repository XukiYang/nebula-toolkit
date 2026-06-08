#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <functional>
#include <memory>
#include <vector>

#include "containers/unpacker.hpp"
#include "logger/logger.hpp"
#include "io/transport/enums.hpp"

namespace nebula {
namespace io {
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

    /// @brief 是否是监听型 handler（收到 EPOLLIN 时调用 HandleNewConnection 而非 HandleEvent）
    /// 默认返回 false，监听型 handler 应覆写为 true
    virtual bool IsListener() const {
        return false;
    }

    /// @brief 监听型 handler 处理新连接（非监听型 handler 无需覆写）
    virtual void HandleNewConnection(const EventContext &ctx) {
        (void)ctx;
    }

    /// @brief 原始数据写入（供 HandleResponseFrame 调用）
    /// 默认实现直接调用 ::write()，需要线程安全的 handler（如 SpHandler）应覆写此方法。
    /// @return 成功写入的字节数，失败返回 -1
    virtual int WriteRaw(const void *data, size_t len) {
        ssize_t written = ::write(Fd(), data, len);
        if (written < 0) {
            return -1;
        }
        return static_cast<int>(written);
    }

    /// @brief 获取 handler 关联的 fd（子类必须实现）
    virtual int Fd() const = 0;

    virtual ~ProtocolHandler() = default;
};

/// @brief TCP协议处理器 
class TcpHandler : public ProtocolHandler {
public:
    TcpHandler(int fd, std::unique_ptr<containers::UnPacker> unpacker)
        : fd_(fd), unpacker_(std::move(unpacker)), should_close_(false) {}

    void SetCallback(ExecCb cb) {
        cb_ = std::move(cb);
    }
    bool ShouldClose() const override {
        return should_close_;
    }
    int Fd() const override {
        return fd_;
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

    ~TcpHandler() override = default;

private:
    const int                             fd_;
    bool                                  should_close_;
    ExecCb                                cb_;
    std::unique_ptr<containers::UnPacker> unpacker_;
    std::vector<std::vector<uint8_t>>     packs_;

    void ProcessReadableEvent(const EventContext &ctx) {
        while (true) {
            auto [buffer, capacity] = unpacker_->GetLinearWriteSpace();
            if (capacity == 0) {
                LOGP_MSG("Buffer full on fd:%d,write space:%d,read space:%d", fd_, unpacker_->AvailableToWrite(),
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
    int Fd() const override {
        return fd_;
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
                    LOGP_MSG("Buffer full on fd:%d,write space:%d,read space:%d", fd_, unpacker_->AvailableToWrite(),
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

/// @brief TCP 监听型协议处理器
/// 封装 TCP accept 逻辑，从 ReactorCore 中解耦出来。
/// 收到 EPOLLIN 时通过 HandleNewConnection 接受新连接，
/// 并为每个连接创建 TcpHandler，通过注册回调注册到 ReactorCore。
///
/// 设计说明：不直接持有 TcpWriteManager 指针，而是通过 RegisterCb 回调
/// 将"注册新连接"的全部逻辑（包括 write_manager 注册 + ReactorCore 注册）
/// 委托给 ReactorCore::ListenTcp() 设置的回调，避免循环依赖。
class TcpListenerHandler : public ProtocolHandler {
public:
    /// @brief 注册回调类型：接受新连接 fd 和 handler，完成注册
    using RegisterCb = std::function<void(int fd, std::unique_ptr<ProtocolHandler> handler)>;

    /// @brief 构造 TCP 监听处理器
    /// @param listen_fd    监听 socket fd
    /// @param head_key     解包器头标识
    /// @param tail_key     解包器尾标识
    /// @param data_sz_cb   数据大小回调（可选）
    /// @param check_sz_cb  校验回调（可选）
    /// @param exec_cb      业务执行回调
    /// @param buffer_size  解包器缓冲区大小
    TcpListenerHandler(int listen_fd,
                       containers::HeadKey head_key,
                       containers::TailKey tail_key,
                       containers::DataSzCb data_sz_cb,
                       containers::CheckValidCb check_sz_cb,
                       ExecCb exec_cb,
                       size_t buffer_size)
        : listen_fd_(listen_fd),
          head_key_(std::move(head_key)),
          tail_key_(std::move(tail_key)),
          data_sz_cb_(std::move(data_sz_cb)),
          check_sz_cb_(std::move(check_sz_cb)),
          exec_cb_(std::move(exec_cb)),
          buffer_size_(buffer_size) {}

    /// @brief 标记为监听型 handler
    bool IsListener() const override {
        return true;
    }

    int Fd() const override {
        return listen_fd_;
    }

    /// @brief 处理新 TCP 连接：accept4 循环 + 创建 TcpHandler
    /// @param ctx 事件上下文（包含 reactor 的 epoll_fd 和 timer_scheduler）
    void HandleNewConnection(const EventContext &ctx) override {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t   addr_len = sizeof(client_addr);

            // accept4 循环：边缘触发模式下必须读到 EAGAIN
            int conn_fd = accept4(listen_fd_, (sockaddr *)&client_addr, &addr_len, SOCK_NONBLOCK);

            if (conn_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 所有连接已接受
                perror("accept4");
                continue;
            }

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            LOGP_MSG("Accepted connection [fd:%d] from %s:%d", conn_fd, ip_str, ntohs(client_addr.sin_port));

            // 为新连接创建独立的解包器
            std::unique_ptr<containers::UnPacker> unpacker;
            if (data_sz_cb_ && check_sz_cb_) {
                unpacker = containers::UnPacker::CreateWithCallbacks(
                    head_key_, tail_key_, data_sz_cb_, check_sz_cb_, buffer_size_);
            } else {
                unpacker = containers::UnPacker::CreateHeadTail(head_key_, tail_key_, buffer_size_);
            }

            // 创建 TCP 数据处理器
            auto handler = std::make_unique<TcpHandler>(conn_fd, std::move(unpacker));
            if (exec_cb_) {
                handler->SetCallback(exec_cb_);
            }

            // 通过注册回调将新连接注册到 ReactorCore（含 write_manager 注册）
            if (register_cb_) {
                register_cb_(conn_fd, std::move(handler));
            } else {
                // 未设置注册回调时关闭 fd，防止资源泄漏
                close(conn_fd);
            }
        }
    }

    /// @brief 监听型 handler 不处理普通数据事件
    void HandleEvent(const EventContext &ctx) override {
        (void)ctx;
    }

    /// @brief 设置注册回调
    /// @param cb 回调函数，由 ReactorCore::ListenTcp() 注入
    void SetRegisterCb(RegisterCb cb) {
        register_cb_ = std::move(cb);
    }

    ~TcpListenerHandler() override = default;

private:
    int listen_fd_;

    // 解包器参数（每个新连接创建独立的 UnPacker）
    containers::HeadKey      head_key_;
    containers::TailKey      tail_key_;
    containers::DataSzCb     data_sz_cb_;
    containers::CheckValidCb check_sz_cb_;
    ExecCb                   exec_cb_;
    size_t                   buffer_size_;

    RegisterCb register_cb_;  // handler 注册回调
};

}  // namespace transport
}  // namespace io
}  // namespace nebula
