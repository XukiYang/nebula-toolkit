#pragma once
#include "../../containers/unpacker.hpp"
#include "../../threading/timer_scheduler.hpp"
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

/// @brief 执行回调
using ExecCb = std::function<void(std::vector<std::vector<uint8_t>> &packs)>;
using NewConnCb = std::function<void(int new_fd)>;
/// @brief 协议处理器统一接口
class ProtocolHandler {
public:
  explicit ProtocolHandler(containers::UnPacker *unpacker)
      : unpacker_(unpacker) {}
  virtual void HandleEvent(const Event &event) = 0;

protected:
  /// @brief 设置回调
  /// @param exec_cb
  void setCb(ExecCb &&exec_cb) { exec_cb_ = std::forward<ExecCb>(exec_cb); };

  containers::UnPacker *unpacker_;
  ExecCb exec_cb_;
  std::vector<std::vector<uint8_t>> packs_;
};

/// @brief 传输层 Tcp数据处理器
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
          unpacker_->CommitWriteSize(bytes_read);
          unpacker_->Get(packs_);
          if (packs_.size() > 1)
            exec_cb_(packs_);
        }
      }
    }
  }
};

/// @brief 传输层 新连接处理器
class AcceptorHandler : public ProtocolHandler {
public:
  using NewConnectCb = std::function<void(int new_fd)>;
  AcceptorHandler(NewConnectCb &&new_connect_cb)
      : ProtocolHandler(nullptr), new_connect_cb_(std::move(new_connect_cb)){};

  /// @brief 新连接处理
  /// @param event
  void HandleEvent(const Event &event) override {
    if (event.event_flags == kReadable) {
      // 持续接收连接知道没有更多连接
      while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept4(event.fd, (sockaddr *)&client_addr, &addr_len,
                                SOCK_NONBLOCK); // 非阻塞

        if (client_fd == -1) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break; // 无更多新连接
          } else {
            perror("accept");
            break;
          }
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("Accepted connection from %s:%d on fd=%d\n", client_ip,
               ntohs(client_addr.sin_port), client_fd);

        // 回调通知新连接到来
        new_connect_cb_(client_fd);
      }
    }
  };

public:
  NewConnectCb new_connect_cb_;
};

/// @brief Socket创建
class SocketCreator {
public:
  /// @brief 创建TCP套接字并绑定到指定地址
  /// @param ip 绑定IP地址（空字符串表示INADDR_ANY）
  /// @param port 绑定端口
  /// @param listen_backlog 监听队列长度（0表示不监听）
  /// @return 成功返回套接字描述符，失败返回-1
  static int CreateTcpSocket(std::string ip, uint16_t port,
                             int listen_backlog = 0) {
    // 创建非阻塞TCP套接字
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_fd == -1) {
      perror("TCP socket creation failed");
      return -1;
    }

    // 设置地址重用选项
    int opt = 1;
    setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 配置套接字地址结构
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // 处理IP地址
    if (ip.empty() || ip == "*") {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
      if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(socket_fd);
        return -1;
      }
    }

    // 绑定套接字
    if (bind(socket_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("TCP bind failed");
      close(socket_fd);
      return -1;
    }

    // 如果需要监听，设置监听队列
    if (listen_backlog > 0) {
      if (listen(socket_fd, listen_backlog) < 0) {
        perror("TCP listen failed");
        close(socket_fd);
        return -1;
      }
    }

    return socket_fd;
  }

  /// @brief 创建UDP套接字并绑定到指定地址
  /// @param ip 绑定IP地址（空字符串表示INADDR_ANY）
  /// @param port 绑定端口
  /// @param enable_reuse 是否启用地址重用
  /// @return 成功返回套接字描述符，失败返回-1
  static int CreateUdpSocket(std::string ip, uint16_t port,
                             bool enable_reuse = true) {
    // 创建非阻塞UDP套接字
    int socket_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socket_fd == -1) {
      perror("UDP socket creation failed");
      return -1;
    }

    // 设置地址重用选项
    if (enable_reuse) {
      int opt = 1;
      setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }

    // 配置套接字地址结构
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // 处理IP地址
    if (ip.empty() || ip == "*") {
      addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
      if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(socket_fd);
        return -1;
      }
    }

    // 绑定套接字
    if (bind(socket_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      perror("UDP bind failed");
      close(socket_fd);
      return -1;
    }

    return socket_fd;
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
      : max_events_(max_events) {
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

    // 设置套接字非阻塞
    fcntl(ev.data.fd, F_SETFL, fcntl(ev.data.fd, F_GETFL) | O_NONBLOCK);

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

  /// @brief 添加Tcp监听
  /// @param port
  /// @param new_conn_cb
  /// @return
  bool AddTcpListener(int listen_fd, NewConnCb new_conn_cb) {
    // 创建并注册AcceptorHandler
    auto acceptor = std::make_unique<AcceptorHandler>(std::move(new_conn_cb));
    RegisterProtocol(listen_fd, std::move(acceptor), TriggerMode::kLt);
    listeners_.insert(listen_fd);
    return true;
  }

  /// @brief 移除监听端口
  void RemoveListener(int listen_fd) {
    if (listeners_.find(listen_fd) != listeners_.end()) {
      UnRegisterSocket(listen_fd);
      listeners_.erase(listen_fd);
    }
  }

private:
  int epoll_fd_ = -1;
  uint64_t max_events_ = 64;
  std::unordered_map<int, std::unique_ptr<ProtocolHandler>> protocol_handlers_;
  std::unordered_set<int> listeners_; // 监听套接字集合
};
}; // namespace net