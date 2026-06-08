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
#include <unordered_set>
#include <vector>

#include "containers/lockfree_queue.hpp"
#include "logger/logger.hpp"
#include "net/core/tcp_write_manager.hpp"
#include "net/transport/enums.hpp"
#include "net/transport/protocol_handler.hpp"

namespace nebula {
namespace net {
namespace core {
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

    /// @brief 添加套接字到epoll并注册协议处理器
    /// @param fd
    /// @param handler
    /// @param is_listener
    void RegisterProtocol(int fd, std::unique_ptr<transport::ProtocolHandler> handler, bool is_listener = false) {
        // 配置epoll事件 水平触发或边缘触发
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = fd;

        // 设置非阻塞模式
        int flags = fcntl(fd, F_GETFL);
        if (flags == -1) throw std::runtime_error("fcntl F_GETFL");
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) throw std::runtime_error("fcntl O_NONBLOCK");

        // 添加到epoll
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) throw std::runtime_error("epoll_ctl ADD");

        // 存储处理器
        protocol_handlers_[fd] = std::move(handler);

        // 标记监听套接字
        if (is_listener) {
            listeners_.insert(fd);
            LOGP_MSG("Registered LISTENER on fd:%d", fd);
        } else {
            LOGP_MSG("Registered CONNECTION on fd:%d", fd);
        }
    }

    /// @brief 事件循环机制
    void Run() {
        std::vector<epoll_event> events(max_events_);
        if (timer_scheduler_) {
            timer_scheduler_->Start();
        }
        while (running_) {
            // 先消费业务线程产出的响应任务，再进入epoll等待。
            // 这样可以保证“业务完成 -> 回包”不会被网络空闲阻塞住。
            ConsumeEventResponses();

            // 等待事件
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

                // 如果是监听套接字（TCP）
                if (listeners_.find(fd) != listeners_.end()) {
                    HandleNewConnections(fd);
                    continue;
                }

                // TCP 发送采用”缓冲区 + EPOLLOUT 驱动”，避免一次 send 发不完导致丢数据。
                if (revents & EPOLLOUT) {
                    tcp_write_manager_.Flush(fd);
                }

                // 查找处理器
                auto it = protocol_handlers_.find(fd);
                if (it != protocol_handlers_.end()) {
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
    }

    void Stop() {
        running_ = false;
    }

    /// @brief 设置连接处理器参数
    /// @param head_key
    /// @param tail_key
    /// @param data_sz_cb_
    /// @param check_sz_cb_
    /// @param exec_cb_
    /// @param buffer_size
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
        // 使用shared_ptr<Frame>而不是Frame值拷贝：
        // Frame内部包含BytesStream（不可拷贝），队列传指针可避免拷贝限制与大对象搬运开销。
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
        listeners_.erase(fd);
        tcp_write_manager_.UnregisterFd(fd);
        close(fd);
        LOGP_MSG("Unregistered fd:%d", fd);
    }

    /// @brief 处理TCP新连接到来
    /// @param listen_fd
    void HandleNewConnections(int listen_fd) {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t   addr_len = sizeof(client_addr);

            int conn_fd = accept4(listen_fd, (sockaddr *)&client_addr, &addr_len, SOCK_NONBLOCK);

            if (conn_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("accept4");
                continue;
            }

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            LOGP_MSG("Accepted connection [fd:%d] from %s:%d", conn_fd, ip_str, ntohs(client_addr.sin_port));
            tcp_write_manager_.RegisterFd(conn_fd);

            // 为连接创建处理程序
            CreateConnHandler(conn_fd);
        }
    }

    /// @brief 创建处理器
    /// @param conn_fd
    void CreateConnHandler(int conn_fd) {
        // 创建解包器（每个连接独立）
        std::unique_ptr<containers::UnPacker> unpacker;
        if (data_sz_cb_ && check_sz_cb_) {
            // 使用带回调的解包器
            unpacker = containers::UnPacker::CreateWithCallbacks(head_key_, tail_key_, data_sz_cb_, check_sz_cb_,
                                                                 buffer_size_);
        } else {
            // 使用基本解包器
            unpacker = containers::UnPacker::CreateHeadTail(head_key_, tail_key_, buffer_size_);
        }
        // 创建TCP处理器
        auto handler = std::make_unique<transport::TcpHandler>(conn_fd, std::move(unpacker));
        // 设置业务执行回调
        if (exec_cb_) {
            handler->SetCallback(exec_cb_);
        }

        // 注册新连接
        RegisterProtocol(conn_fd, std::move(handler));
    }

    // epoll与事件循环相关
    int               epoll_fd_   = -1;
    uint64_t          max_events_ = 64;
    std::atomic<bool> running_{true};

    // 解包器参数
    containers::HeadKey      head_key_{};
    containers::TailKey      tail_key_{};
    containers::DataSzCb     data_sz_cb_  = nullptr;
    containers::CheckValidCb check_sz_cb_ = nullptr;
    size_t                   buffer_size_ = 0;

    // 处理器业务执行回调
    transport::ExecCb exec_cb_ = nullptr;

    // 协议处理器映射与监听套接字集合
    std::unordered_map<int, std::unique_ptr<transport::ProtocolHandler>> protocol_handlers_;
    std::unordered_set<int>                                              listeners_;

    // TCP 写缓冲管理（拆分自 ReactorCore，职责单一化）
    TcpWriteManager tcp_write_manager_;

    // 定时线程池依赖
    std::shared_ptr<threading::TimerScheduler> timer_scheduler_;

    // 响应任务队列
    std::shared_ptr<containers::LockFreeQueue<std::shared_ptr<transport::event_response::Frame>>> event_response_queue_;
};

}  // namespace core
}  // namespace net
}  // namespace nebula
