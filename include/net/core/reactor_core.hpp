#pragma once
#include "../../containers/unpacker.hpp"
// #include "../../threading/timer_scheduler.hpp"
#include "../transport/protocol_handler.hpp"

#include "../transport/protocol_handler.hpp"
#include "../transport/socket_creator.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/epoll.h>
#include <unistd.h>
#include <utility>

namespace net {

/// @brief Reactor引擎
class ReactorCore {
public:
  /// @brief 依赖最大事件与线程池线程数的构造方式
  /// @param max_events
  /// @param max_threads
  ReactorCore(uint64_t max_events = 64,
              uint64_t max_threads = std::thread::hardware_concurrency())
      : max_events_(max_events) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1)
      throw std::runtime_error("epoll_create");
    LOG_MSG("into ReactorCore");
  }

  ~ReactorCore() { close(epoll_fd_); }

  /// @brief 注册协议处理器
  void RegisterProtocol(int socket_fd,
                        std::unique_ptr<ProtocolHandler> protocol_handler,
                        TriggerMode mode = TriggerMode::kEt,
                        bool is_listener = false) {
    // 转换mode为标识掩码为水平还是边缘触发
    uint32_t events = EPOLLIN;
    if (mode == kEt)
      events |= EPOLLET;

    // 准备epoll_event
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = socket_fd; // 存储原始fd

    // 设置套接字非阻塞
    fcntl(ev.data.fd, F_SETFL, fcntl(ev.data.fd, F_GETFL) | O_NONBLOCK);

    // 将该socket加入实例监控
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket_fd, &ev) == -1) {
      throw std::runtime_error("epoll_ctl add");
    }

    // 存储映射关系
    protocol_handlers_[socket_fd] = std::move(protocol_handler);

    // 进行监听
    if (is_listener)
      listen_fds_.insert(socket_fd);
  };

  /// @brief 移除已注册的fd以及处理器
  /// @param socket_fd
  void UnRegisterSocket(int socket_fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, socket_fd, nullptr) == -1) {
      perror("epoll_ctl del");
    }

    protocol_handlers_.erase(socket_fd);

    // 从内核中去除该套接字
    close(socket_fd);
  }

  /// @brief 主事件循环
  void Run() {
    epoll_event events[max_events_];
    while (true) {
      // 阻塞等待事件
      int nfds = epoll_wait(epoll_fd_, events, max_events_, -1);
      if (nfds == -1) {
        perror("epoll_wait");
        break;
      }
      LOGP_MSG("epoll_wait,nfds:%d");

      // 处理所有就绪事件
      for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;
        uint32_t revents = events[i].events;

        // 检查事件类型
        Event ev;
        if (revents & EPOLLIN)
          ev.event_flags = kReadable;
        if (revents & EPOLLOUT)
          ev.event_flags = kWritable;
        if (revents & EPOLLERR)
          ev.event_flags = kError;
        if (revents & EPOLLHUP)
          ev.event_flags = kHangUp;
        ev.fd = fd;

        // 查找处理器
        auto it = protocol_handlers_.find(fd);
        if (it != protocol_handlers_.end()) {
          LOGP_MSG("event coming socket:%d", fd);
          it->second->HandleEvent(epoll_fd_, ev);
        }
      }
    }
  }

private:
  int epoll_fd_ = -1;        // epoll实例fd
  uint64_t max_events_ = 64; // epoll最大事件数
  std::unordered_map<int, std::unique_ptr<ProtocolHandler>>
      protocol_handlers_; // fd与处理器的映射
  std::unordered_set<int> listen_fds_;
};
}; // namespace net
