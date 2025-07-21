#pragma once
#include "../../logger/logger.hpp"
#include "../transport/enums.hpp"
#include "../transport/protocol_handler.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

namespace net {

class ReactorCore {
public:
  ReactorCore(uint64_t max_events = 64) : max_events_(max_events) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1)
      throw std::runtime_error("epoll_create failed");
    LOGP_MSG("ReactorCore initialized with max_events: %lu", max_events);
  }

  ~ReactorCore() {
    if (epoll_fd_ >= 0)
      close(epoll_fd_);

    // 清理所有处理器
    for (auto &handler : protocol_handlers_) {
      close(handler.first);
    }
  }

  /// @brief 添加套接字到epoll并注册协议处理器
  /// @param fd
  /// @param handler
  /// @param mode
  /// @param is_listener
  void RegisterProtocol(int fd, std::unique_ptr<ProtocolHandler> handler,
                        TriggerMode mode = TriggerMode::kEt,
                        bool is_listener = false) {
    // 配置epoll事件 水平触发或边缘触发
    epoll_event ev{};
    ev.events = EPOLLIN | (mode == TriggerMode::kEt ? EPOLLET : 0);
    ev.data.fd = fd;

    // 设置非阻塞模式
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
      throw std::runtime_error("fcntl F_GETFL");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
      throw std::runtime_error("fcntl O_NONBLOCK");

    // 添加到epoll
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
      throw std::runtime_error("epoll_ctl ADD");

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
    epoll_event events[max_events_];
    while (running_) {
      // 等待事件
      int nfds = epoll_wait(epoll_fd_, events, max_events_, -1);
      if (nfds == -1) {
        if (errno == EINTR)
          continue; // 信号中断，重新等待
        perror("epoll_wait");
        break;
      }

      LOGP_MSG("Processing %d events", nfds);

      for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        uint32_t revents = events[i].events;

        // 构造事件对象
        Event ev;
        ev.fd = fd;
        ev.event_flags = static_cast<EventFlags>(0); // 初始化为无事件

        if (revents & EPOLLIN)
          ev.event_flags =
              static_cast<EventFlags>(ev.event_flags | EventFlags::kReadable);
        if (revents & EPOLLOUT)
          ev.event_flags =
              static_cast<EventFlags>(ev.event_flags | EventFlags::kWritable);
        if (revents & EPOLLERR)
          ev.event_flags =
              static_cast<EventFlags>(ev.event_flags | EventFlags::kError);
        if (revents & EPOLLHUP)
          ev.event_flags =
              static_cast<EventFlags>(ev.event_flags | EventFlags::kHangUp);

        // 如果是监听套接字（TCP）
        if (listeners_.find(fd) != listeners_.end()) {
          HandleNewConnections(fd);
          continue;
        }

        // 查找处理器
        auto it = protocol_handlers_.find(fd);
        if (it != protocol_handlers_.end()) {
          try {
            it->second->HandleEvent(epoll_fd_, ev);
          } catch (const std::exception &e) {
            LOGP_MSG("Error handling fd:%d - %s", fd, e.what());
            UnregisterFd(fd);
          }

          // 检查连接是否需要关闭
          if (it->second->ShouldClose()) {
            UnregisterFd(fd);
          }
        }
      }
    }
  }

  void Stop() { running_ = false; }

  /// @brief 设置连接处理器参数
  /// @param head_key
  /// @param tail_key
  /// @param data_sz_cb_
  /// @param check_sz_cb_
  /// @param exec_cb_
  /// @param buffer_size
  void SetConnHandlerParams(containers::HeadKey &&head_key,
                            containers::TailKey &&tail_key,
                            containers::DataSzCb data_sz_cb = nullptr,
                            containers::CheckValidCb check_sz_cb = nullptr,
                            ExecCb exec_cb = nullptr,
                            size_t buffer_size = 1024) {
    head_key_ = std::move(head_key);
    tail_key_ = std::move(tail_key);
    data_sz_cb_ = std::move(data_sz_cb);
    check_sz_cb_ = std::move(check_sz_cb);
    exec_cb_ = std::move(exec_cb);
    buffer_size_ = buffer_size;
  }

private:
  void UnregisterFd(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
      perror("epoll_ctl del");
    }

    protocol_handlers_.erase(fd);
    listeners_.erase(fd);
    close(fd);
    LOGP_MSG("Unregistered fd:%d", fd);
  }

  /// @brief 处理TCP新连接到来
  /// @param listen_fd
  void HandleNewConnections(int listen_fd) {
    while (true) {
      sockaddr_in client_addr{};
      socklen_t addr_len = sizeof(client_addr);

      int conn_fd = accept4(listen_fd, (sockaddr *)&client_addr, &addr_len,
                            SOCK_NONBLOCK);

      if (conn_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        perror("accept4");
        continue;
      }

      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
      LOGP_MSG("Accepted connection [fd:%d] from %s:%d", conn_fd, ip_str,
               ntohs(client_addr.sin_port));

      // 为连接创建处理程序
      CreateConnHandler(conn_fd);
    }
  }

  /// @brief 创建处理器
  /// @param conn_fd
  void CreateConnHandler(int conn_fd) {
    // 创建解包器（每个连接独立）
    auto unpacker = containers::UnPacker::CreateWithCallbacks(
        std::forward<containers::HeadKey>(head_key_),
        std::forward<containers::TailKey>(tail_key_),
        std::forward<containers::DataSzCb>(data_sz_cb_),
        std::forward<containers::CheckValidCb>(check_sz_cb_), 1024);

    // 创建TCP处理器
    auto handler = std::make_unique<TcpHandler>(conn_fd, std::move(unpacker));
    // 设置业务执行回调
    handler->SetCallback(exec_cb_);

    // 注册新连接
    RegisterProtocol(conn_fd, std::move(handler), TriggerMode::kEt, false);
  }

  // epoll与事件循环相关
  int epoll_fd_ = -1;
  uint64_t max_events_ = 64;
  std::atomic<bool> running_{true};

  // 解包器参数
  containers::HeadKey head_key_{};
  containers::TailKey tail_key_{};
  containers::DataSzCb data_sz_cb_ = nullptr;
  containers::CheckValidCb check_sz_cb_ = nullptr;
  size_t buffer_size_ = 1024;

  // 处理器业务执行回调
  ExecCb exec_cb_ = nullptr;

  // 协议处理器映射与TCP监听套接字
  std::unordered_map<int, std::unique_ptr<ProtocolHandler>> protocol_handlers_;
  std::unordered_set<int> listeners_;
};

} // namespace net