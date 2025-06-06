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
  virtual void HandleEvent(int epoll_fd, const Event &event) = 0;

protected:
  /// @brief 设置回调
  /// @param exec_cb
  void SetCallBack(ExecCb &&exec_cb) {
    exec_cb_ = std::forward<ExecCb>(exec_cb);
  };

  virtual void AddSocketFd(int epoll_fd, int fd) = 0;

  containers::UnPacker *unpacker_;
  ExecCb exec_cb_;
  std::vector<std::vector<uint8_t>> packs_;
};

/// @brief 传输层 Tcp数据处理器
class TcpHandler : public ProtocolHandler {
public:
  TcpHandler(containers::UnPacker *unpacker) : ProtocolHandler(unpacker) {}
  void HandleEvent(int epoll_fd, const Event &event) override {
    // 新连接到来
    if (event.fd == epoll_fd && event.event_flags == EventFlags::kReadable) {
      LOG_MSG("new connet!!!");
      struct sockaddr_in client_address;
      socklen_t client_addrlength = sizeof(client_address);
      int conn_fd = accept(event.fd, (struct sockaddr *)&client_address,
                           &client_addrlength);
      //将新的连接套接字也注册可读事件
      AddSocketFd(epoll_fd, conn_fd);
    } else if (event.event_flags == EventFlags::kReadable) {
      LOG_MSG("read event!!!");
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

private:
  void AddSocketFd(int epoll_fd, int fd) override {
    struct epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    int old_option = fcntl(epoll_fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(epoll_fd, F_SETFL, new_option);
  }
};

} // namespace net
