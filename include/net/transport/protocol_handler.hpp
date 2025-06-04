#pragma once
#include "../../containers/unpacker.hpp"
#include "../../threading/timer_scheduler.hpp"
#include "enums.hpp"
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
} // namespace net
