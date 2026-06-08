#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "containers/lockfree_queue.hpp"
#include "logger/logger.hpp"
#include "io/core/tcp_write_manager.hpp"
#include "io/transport/enums.hpp"
#include "io/transport/protocol_handler.hpp"
#include "io/transport/socket_creator.hpp"

namespace nebula {
namespace io {
namespace core {

/// @brief 协议无关的 epoll 事件反应器
/// 统一管理 TCP/UDP/串口等多种协议的 I/O 事件分发。
/// 监听型 handler（IsListener() == true）收到 EPOLLIN 时调用 HandleNewConnection；
/// 普通 handler 收到事件时调用 HandleEvent。
class ReactorCore {
public:
    ReactorCore(uint64_t max_events = 64) : max_events_(max_events) {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) throw std::runtime_error("epoll_create failed");
        tcp_write_manager_.Init(epoll_fd_);
        LOGP_MSG("ReactorCore initialized with max_events: %lu", max_events);
    }

    ~ReactorCore() {
        if (epoll_fd_ >= 0) close(epoll_fd_);

        // 清理所有处理器及其写缓冲状态
        for (auto &handler : protocol_handlers_) {
            tcp_write_manager_.UnregisterFd(handler.first);
            close(handler.first);
        }
    }

    /// @brief 注册协议处理器到 epoll
    /// @param fd              要监听的文件描述符
    /// @param handler         协议处理器（所有权转移给 ReactorCore）
    /// @param edge_triggered  是否使用边缘触发（默认 true，串口等设备用 false 即水平触发）
    void RegisterProtocol(int fd, std::unique_ptr<transport::ProtocolHandler> handler,
                          bool edge_triggered = true) {
        // 配置 epoll 事件：默认 EPOLLIN，可选边缘触发
        epoll_event ev{};
        ev.events  = EPOLLIN;
        if (edge_triggered) ev.events |= EPOLLET;
        ev.data.fd = fd;

        // 设置非阻塞模式
        int flags = fcntl(fd, F_GETFL);
        if (flags == -1) throw std::runtime_error("fcntl F_GETFL");
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) throw std::runtime_error("fcntl O_NONBLOCK");

        // 添加到 epoll 实例
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) throw std::runtime_error("epoll_ctl ADD");

        // 存储处理器映射
        protocol_handlers_[fd] = std::move(handler);

        // 根据 handler 类型记录日志
        if (protocol_handlers_[fd]->IsListener()) {
            LOGP_MSG("Registered LISTENER on fd:%d (edge=%d)", fd, edge_triggered);
        } else {
            LOGP_MSG("Registered HANDLER on fd:%d (edge=%d)", fd, edge_triggered);
        }
    }

    /// @brief 创建 TCP 监听 socket 并注册 TcpListenerHandler
    /// @param ip        绑定 IP
    /// @param port      绑定端口
    /// @param backlog   listen 队列长度
    /// @return 监听 fd，失败返回 -1
    int ListenTcp(const std::string &ip, uint16_t port, int backlog = 128) {
        int fd = transport::SocketCreator::CreateTcpSocket(ip, port, true, backlog);
        if (fd < 0) return -1;

        auto listener = std::make_unique<transport::TcpListenerHandler>(
            fd, head_key_, tail_key_, data_sz_cb_, check_sz_cb_, exec_cb_,
            buffer_size_);

        // 设置注册回调：TcpListenerHandler accept 新连接后通过此回调注册到 ReactorCore。
        // 回调中同时完成 write_manager 注册（分配 conn_id）和 epoll 注册。
        listener->SetRegisterCb([this](int conn_fd, std::unique_ptr<transport::ProtocolHandler> h) {
            tcp_write_manager_.RegisterFd(conn_fd);
            RegisterProtocol(conn_fd, std::move(h));
        });

        RegisterProtocol(fd, std::move(listener));
        return fd;
    }

    /// @brief 事件循环机制
    void Run() {
        std::vector<epoll_event> events(max_events_);
        if (timer_scheduler_) {
            timer_scheduler_->Start();
        }
        while (running_) {
            // 先消费业务线程产出的响应任务，再进入 epoll 等待。
            // 这样可以保证"业务完成 -> 回包"不会被网络空闲阻塞住。
            ConsumeEventResponses();

            // 等待事件（10ms 超时，保证响应队列能被及时消费）
            int nfds = epoll_wait(epoll_fd_, events.data(), static_cast<int>(events.size()), 10);
            if (nfds == -1) {
                if (errno == EINTR) continue;  // 信号中断，重新等待
                perror("epoll_wait");
                break;
            }
            if (nfds == 0) {
                continue;
            }

            LOGP_MSG("Processing %d events", nfds);

            for (int i = 0; i < nfds; ++i) {
                int      fd      = events[static_cast<size_t>(i)].data.fd;
                uint32_t revents = events[static_cast<size_t>(i)].events;

                // 查找对应的协议处理器
                auto it = protocol_handlers_.find(fd);
                if (it == protocol_handlers_.end()) continue;

                // 构造事件上下文，聚合 IO 事件所需的全部信息
                transport::EventContext ctx;
                ctx.epoll_fd        = epoll_fd_;
                ctx.fd              = fd;
                ctx.conn_id         = tcp_write_manager_.GetConnId(fd);
                ctx.timer_scheduler = timer_scheduler_;
                ctx.event_flags     = static_cast<transport::EventFlags>(0);

                if (revents & EPOLLIN)
                    ctx.event_flags =
                        static_cast<transport::EventFlags>(ctx.event_flags | transport::EventFlags::kReadable);
                if (revents & EPOLLOUT)
                    ctx.event_flags =
                        static_cast<transport::EventFlags>(ctx.event_flags | transport::EventFlags::kWritable);
                if (revents & EPOLLERR)
                    ctx.event_flags =
                        static_cast<transport::EventFlags>(ctx.event_flags | transport::EventFlags::kError);
                if (revents & EPOLLHUP)
                    ctx.event_flags =
                        static_cast<transport::EventFlags>(ctx.event_flags | transport::EventFlags::kHangUp);

                // 监听型 handler：收到 EPOLLIN 时调用 HandleNewConnection 而非 HandleEvent
                if (it->second->IsListener()) {
                    it->second->HandleNewConnection(ctx);
                    continue;
                }

                // TCP 写缓冲驱动：EPOLLOUT 时冲刷待发数据（仅对 TCP handler 生效）
                if (ctx.event_flags & transport::EventFlags::kWritable) {
                    tcp_write_manager_.Flush(fd);
                }

                // 普通数据事件分发
                try {
                    it->second->HandleEvent(ctx);
                } catch (const std::exception &e) {
                    LOGP_MSG("Error handling fd:%d - %s", fd, e.what());
                    UnregisterFd(fd);
                    continue;
                }

                // 检查连接是否需要关闭
                if (it->second->ShouldClose()) {
                    UnregisterFd(fd);
                }
            }
        }
    }

    void Stop() {
        running_ = false;
    }

    /// @brief 设置连接处理器参数（供 TcpListenerHandler 创建新连接的 UnPacker 使用）
    /// @param head_key     解包器头标识
    /// @param tail_key     解包器尾标识
    /// @param data_sz_cb   数据大小回调
    /// @param check_sz_cb  校验回调
    /// @param exec_cb      业务执行回调
    /// @param buffer_size  解包器缓冲区大小
    void SetConnHandlerParams(containers::HeadKey head_key, containers::TailKey tail_key,
                              containers::DataSzCb data_sz_cb, containers::CheckValidCb check_sz_cb,
                              transport::ExecCb exec_cb, size_t buffer_size = 1024) {
        head_key_    = std::move(head_key);
        tail_key_    = std::move(tail_key);
        data_sz_cb_  = std::move(data_sz_cb);
        check_sz_cb_ = std::move(check_sz_cb);
        exec_cb_     = std::move(exec_cb);
        buffer_size_ = buffer_size;
    }

    /// @brief 注入定时线程池依赖
    /// @param timer_scheduler
    void SetTimerScheduler(std::shared_ptr<threading::TimerScheduler> timer_scheduler) {
        timer_scheduler_ = std::move(timer_scheduler);
    };

    /// @brief 设置业务回传队列
    void SetEventResponseQueue(
        std::shared_ptr<containers::LockFreeQueue<std::shared_ptr<transport::event_response::Frame>>> queue) {
        // 使用 shared_ptr<Frame> 而不是 Frame 值拷贝：
        // Frame 内部包含 BytesStream（不可拷贝），队列传指针可避免拷贝限制与大对象搬运开销。
        event_response_queue_ = std::move(queue);
    }

private:

    void ConsumeEventResponses() {
        if (!event_response_queue_) {
            return;
        }

        std::shared_ptr<transport::event_response::Frame> frame;
        while (event_response_queue_->Pop(frame) == containers::Result::kSuccess) {
            if (frame) {
                HandleResponseFrame(*frame);
            }
        }
    }

    /// @brief 处理业务线程投递的响应帧
    /// 根据协议类型分发：UDP 直接 sendto，串口直接 write，TCP 委托写缓冲管理器
    void HandleResponseFrame(const transport::event_response::Frame &frame) {
        const auto proto = frame.head.proto_type;
        const auto act   = frame.head.opt_action;

        // UDP 直接发送，无需写缓冲
        if (proto == transport::event_response::ProtoType::kUdp) {
            if (act == transport::event_response::OptAction::kSend) {
                if (sendto(frame.head.fd, frame.body.data_bytes_stream.Data(),
                           frame.body.data_bytes_stream.Size(), 0,
                           reinterpret_cast<const sockaddr *>(&frame.head.peer_addr),
                           frame.head.peer_addr_len) < 0) {
                    LOGP_MSG("UDP sendto failed on fd:%d, errno:%d", frame.head.fd, errno);
                }
            } else if (act == transport::event_response::OptAction::kClose) {
                UnregisterFd(frame.head.fd);
            }
            return;
        }

        // 串口：通过 handler 的 WriteRaw() 写入（SpHandler 会加锁保护并发安全）
        if (proto == transport::event_response::ProtoType::kSp) {
            if (act == transport::event_response::OptAction::kSend) {
                auto it = protocol_handlers_.find(frame.head.fd);
                if (it != protocol_handlers_.end()) {
                    it->second->WriteRaw(frame.body.data_bytes_stream.Data(),
                                         frame.body.data_bytes_stream.Size());
                }
            } else if (act == transport::event_response::OptAction::kClose) {
                UnregisterFd(frame.head.fd);
            }
            return;
        }

        // TCP 关闭动作
        if (proto == transport::event_response::ProtoType::kTcp &&
            act == transport::event_response::OptAction::kClose) {
            UnregisterFd(frame.head.fd);
            return;
        }

        // TCP 发送：委托写缓冲管理器处理
        if (proto == transport::event_response::ProtoType::kTcp &&
            act == transport::event_response::OptAction::kSend) {
            if (protocol_handlers_.find(frame.head.fd) == protocol_handlers_.end()) {
                return;
            }

            // 防止 fd 复用导致误发：回包携带了旧 conn_id 时直接丢弃
            if (tcp_write_manager_.IsStaleConn(frame.head.fd, frame.head.conn_id)) {
                return;
            }

            tcp_write_manager_.EnqueueWrite(frame.head.fd, frame.body.data_bytes_stream.Data(),
                                            frame.body.data_bytes_stream.Size());
        }
    }

    void UnregisterFd(int fd) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            perror("epoll_ctl del");
        }

        protocol_handlers_.erase(fd);
        tcp_write_manager_.UnregisterFd(fd);
        close(fd);
        LOGP_MSG("Unregistered fd:%d", fd);
    }

    // epoll 与事件循环相关
    int               epoll_fd_   = -1;
    uint64_t          max_events_ = 64;
    std::atomic<bool> running_{true};

    // 解包器参数（供 TcpListenerHandler 创建新连接的 UnPacker 使用）
    containers::HeadKey      head_key_{};
    containers::TailKey      tail_key_{};
    containers::DataSzCb     data_sz_cb_  = nullptr;
    containers::CheckValidCb check_sz_cb_ = nullptr;
    size_t                   buffer_size_ = 0;

    // 处理器业务执行回调
    transport::ExecCb exec_cb_ = nullptr;

    // 协议处理器映射表（fd -> handler）
    std::unordered_map<int, std::unique_ptr<transport::ProtocolHandler>> protocol_handlers_;

    // TCP 写缓冲管理（拆分自 ReactorCore，职责单一化）
    TcpWriteManager tcp_write_manager_;

    // 定时线程池依赖
    std::shared_ptr<threading::TimerScheduler> timer_scheduler_;

    // 响应任务队列
    std::shared_ptr<containers::LockFreeQueue<std::shared_ptr<transport::event_response::Frame>>> event_response_queue_;
};

}  // namespace core
}  // namespace io
}  // namespace nebula
