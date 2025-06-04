#pragma once
#include "../threading/timer_scheduler.hpp"
#include "../transport/strategy.hpp"
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
enum EventFlags {
  kReadable, // 对应EPOLLIN
  kWritable, // 对应EPOLLOUT
  kError,    // 对应EPOLLERR
  kHangUp    // 对应EPOLLHUP
};

enum TriggerMode { kEt, kLt };

struct Event {
  int fd;
  EventFlags event_flags;
};

/// @brief 协议处理器统一接口
class ProtocolHandler {
public:
  explicit ProtocolHandler(containers::UnPacker *unpacker)
      : unpacker_(unpacker) {}
  virtual void HandleEvent(const Event &event) = 0;

protected:
  /// @brief 通用数据提交路径
  void SubmitData(const uint8_t *submit_data) {}
  containers::UnPacker *unpacker_;
};

/// @brief 传输层Tcp协议处理器
class TcpHandler : public ProtocolHandler {
  TcpHandler(containers::UnPacker *unpacker) : ProtocolHandler(unpacker) {}

  void HandleEvent(const Event &event) override {
    if (event.event_flags == EventFlags::kReadable) {

      // 获取线性写指针与可写空间
      std::pair<const uint8_t *, size_t> linear_write_space =
          unpacker_->GetLinearWriteSpace();
      while (true) {
        ssize_t bytes_read = read(event.fd, (void *)linear_write_space.first,
                                  linear_write_space.second - 1);
        if (bytes_read == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }
          perror("read");
          close(event.fd);
          break;
        } else if (bytes_read == 0) {
          printf("Client fd=%d disconnected\n", event.fd);
          close(event.fd);
          break;
        } else {

          // 提交线性写入字节数
          unpacker_->CommitWriteSize(bytes_read);
        }
      }
    }
  }
};

/// @brief Reactor引擎
class ReactorCore {
public:
  /// @brief 依赖最大事件与线程池线程数的构造方式
  /// @param max_events
  /// @param max_threads
  ReactorCore(uint64_t max_events = 64,
              uint64_t max_threads = std::thread::hardware_concurrency())
      : max_events_(max_events), timer_scheduler_(max_threads) {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1)
      throw std::runtime_error("epoll_create");
  }

  ~ReactorCore() { close(epoll_fd_); }

  /// @brief 注册协议处理器
  /// @param socket_fd
  /// @param protocol_handler
  /// @param mode
  void RegisterProtocol(int socket_fd,
                        std::unique_ptr<ProtocolHandler> protocol_handler,
                        TriggerMode mode = TriggerMode::kEt) {
    // 转换mode为标识掩码为水平还是边缘触发
    uint32_t events = EPOLLIN;
    if (mode == kEt)
      events |= EPOLLET;

    // 准备epoll_event
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = socket_fd; // 存储原始fd

    // 将该socket加入实例监控
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, socket_fd, &ev) == -1) {
      throw std::runtime_error("epoll_ctl add");
    }

    // 存储映射关系
    protocol_handlers_[socket_fd] = std::move(protocol_handler);
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
    // 事件容器
    epoll_event events[max_events_];
    while (true) {
      // 阻塞等待所有epoll_fd_实例所有就绪事件
      int nfds = epoll_wait(epoll_fd_, events, max_events_, -1);
      if (nfds == -1) {
        perror("epoll_wait");
        break;
      }

      // 遍历处理就绪事件 转内部事件结构
      for (int i = 0; i < nfds; ++i) {
        Event ev;
        ev.fd = events[i].data.fd;
        if (events[i].events & EPOLLIN)
          ev.event_flags = kReadable;
        if (events[i].events & EPOLLOUT)
          ev.event_flags = kWritable;
        if (events[i].events & EPOLLERR)
          ev.event_flags = kError;
        if (events[i].events & EPOLLHUP)
          ev.event_flags = kHangUp;

        // 找到对应的处理器
        auto it = protocol_handlers_.find(ev.fd);
        if (it != protocol_handlers_.end()) {
          it->second->HandleEvent(ev);
        }
      }
    }
  };

private:
  int epoll_fd_ = -1;
  uint64_t max_events_ = 64;
  std::unordered_map<int, std::unique_ptr<ProtocolHandler>> protocol_handlers_;
  TimerScheduler timer_scheduler_;
};
}; // namespace net