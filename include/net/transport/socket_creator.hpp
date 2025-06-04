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
} // namespace net
