#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../logger/logger.hpp"
#include "../transport/enums.hpp"
#include "../transport/protocol_handler.hpp"

namespace nebula {
namespace net {
namespace core {
class ReactorCore {
public:
    ReactorCore(uint64_t max_events = 64) : max_events_(max_events) {
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) throw std::runtime_error("epoll_create failed");
        LOGP_MSG("ReactorCore initialized with max_events: %lu", max_events);
    }

    ~ReactorCore() {
        if (epoll_fd_ >= 0) close(epoll_fd_);

        // 清理所有处理器
        for (auto &handler : protocol_handlers_) {
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
        fd_event_masks_[fd]    = ev.events;

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
        if (timer_shceduler_) {
            timer_shceduler_->Start();
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

                // 构造事件对象
                transport::Event ev;
                ev.fd          = fd;
                ev.event_flags = static_cast<transport::EventFlags>(0);  // 初始化为无事件

                if (revents & EPOLLIN)
                    ev.event_flags =
                        static_cast<transport::EventFlags>(ev.event_flags | transport::EventFlags::kReadable);
                if (revents & EPOLLOUT)
                    ev.event_flags =
                        static_cast<transport::EventFlags>(ev.event_flags | transport::EventFlags::kWritable);
                if (revents & EPOLLERR)
                    ev.event_flags = static_cast<transport::EventFlags>(ev.event_flags | transport::EventFlags::kError);
                if (revents & EPOLLHUP)
                    ev.event_flags =
                        static_cast<transport::EventFlags>(ev.event_flags | transport::EventFlags::kHangUp);

                // 如果是监听套接字（TCP）
                if (listeners_.find(fd) != listeners_.end()) {
                    HandleNewConnections(fd);
                    continue;
                }

                // TCP发送采用“缓冲区 + EPOLLOUT驱动”，避免一次send发不完导致丢数据。
                if (revents & EPOLLOUT) {
                    FlushTcpPendingWrites(fd);
                }

                // 查找处理器
                auto it = protocol_handlers_.find(fd);
                if (it != protocol_handlers_.end()) {
                    try {
                        it->second->HandleEvent(epoll_fd_, ev, timer_shceduler_);
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
    /// @param timer_shceduler
    void SetTimerScheduler(std::shared_ptr<threading::TimerScheduler> timer_shceduler) {
        timer_shceduler_ = std::move(timer_shceduler);
    };

    /// @brief 设置业务回传队列
    void SetEventResponseQueue(
        std::shared_ptr<containers::LockFreeQueue<std::shared_ptr<transport::event_response::Frame>>> queue) {
        // 使用shared_ptr<Frame>而不是Frame值拷贝：
        // Frame内部包含BytesStream（不可拷贝），队列传指针可避免拷贝限制与大对象搬运开销。
        event_response_queue_ = std::move(queue);
    }

private:
    struct TcpWriteState {
        uint64_t             conn_id = 0;
        std::vector<uint8_t> buffer;
        size_t               sent_offset = 0;
    };

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

        if (proto == transport::event_response::ProtoType::kUdp) {
            if (act == transport::event_response::OptAction::kSend) {
                sendto(frame.head.fd, frame.body.data_bytes_stream.Data(), frame.body.data_bytes_stream.Size(), 0,
                       reinterpret_cast<const sockaddr *>(&frame.head.peer_addr), frame.head.peer_addr_len);
            } else if (act == transport::event_response::OptAction::kClose) {
                UnregisterFd(frame.head.fd);
            }
            return;
        }

        if (proto != transport::event_response::ProtoType::kTcp) {
            return;
        }

        if (act == transport::event_response::OptAction::kClose) {
            UnregisterFd(frame.head.fd);
            return;
        }
        if (act != transport::event_response::OptAction::kSend) {
            return;
        }

        auto fd_it = protocol_handlers_.find(frame.head.fd);
        if (fd_it == protocol_handlers_.end()) {
            return;
        }

        auto id_it = conn_ids_.find(frame.head.fd);
        // 防止fd复用导致误发：回包携带了旧conn_id时直接丢弃。
        if (id_it != conn_ids_.end() && frame.head.conn_id != 0 && frame.head.conn_id != id_it->second) {
            return;
        }

        auto &state = tcp_write_states_[frame.head.fd];
        if (state.conn_id == 0) {
            state.conn_id = (id_it == conn_ids_.end()) ? frame.head.conn_id : id_it->second;
        }

        const auto *data = reinterpret_cast<const uint8_t *>(frame.body.data_bytes_stream.Data());
        const auto  size = frame.body.data_bytes_stream.Size();
        if (size > 0) {
            state.buffer.insert(state.buffer.end(), data, data + size);
        }
        FlushTcpPendingWrites(frame.head.fd);
    }

    void FlushTcpPendingWrites(int fd) {
        auto it = tcp_write_states_.find(fd);
        if (it == tcp_write_states_.end()) {
            return;
        }

        auto &state = it->second;
        while (state.sent_offset < state.buffer.size()) {
            const uint8_t *base = state.buffer.data() + state.sent_offset;
            const size_t   left = state.buffer.size() - state.sent_offset;
            const ssize_t n     = send(fd, base, left, MSG_NOSIGNAL);

            if (n > 0) {
                state.sent_offset += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 当前不可写，打开EPOLLOUT，等待下次可写继续冲刷。
                UpdateWriteEvent(fd, true);
                return;
            }

            UnregisterFd(fd);
            return;
        }

        tcp_write_states_.erase(it);
        // 全部发送完成后关闭EPOLLOUT，避免空转。
        UpdateWriteEvent(fd, false);
    }

    void UpdateWriteEvent(int fd, bool enable) {
        auto it = fd_event_masks_.find(fd);
        if (it == fd_event_masks_.end()) {
            return;
        }

        uint32_t new_mask = it->second;
        if (enable) {
            new_mask |= EPOLLOUT;
        } else {
            new_mask &= ~EPOLLOUT;
        }

        if (new_mask == it->second) {
            return;
        }

        epoll_event ev{};
        ev.events  = new_mask;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
            perror("epoll_ctl mod");
            return;
        }
        it->second = new_mask;
    }

    void UnregisterFd(int fd) {
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            perror("epoll_ctl del");
        }

        protocol_handlers_.erase(fd);
        listeners_.erase(fd);
        fd_event_masks_.erase(fd);
        tcp_write_states_.erase(fd);
        conn_ids_.erase(fd);
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
            conn_ids_[conn_fd] = ++next_conn_id_;

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

    // 协议处理器映射与TCP监听套接字
    std::unordered_map<int, std::unique_ptr<transport::ProtocolHandler>> protocol_handlers_;
    std::unordered_set<int>                                              listeners_;
    std::unordered_map<int, uint32_t>                                    fd_event_masks_;
    std::unordered_map<int, TcpWriteState>                               tcp_write_states_;
    std::unordered_map<int, uint64_t>                                    conn_ids_;
    std::atomic<uint64_t>                                                next_conn_id_{0};

    // 定时线程池依赖
    std::shared_ptr<threading::TimerScheduler> timer_shceduler_;

    // 响应任务队列
    std::shared_ptr<containers::LockFreeQueue<std::shared_ptr<transport::event_response::Frame>>> event_response_queue_;
};

}  // namespace core
}  // namespace net
}  // namespace nebula
