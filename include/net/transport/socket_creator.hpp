#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

class SocketCreator {
public:
  /// @brief 创建TCP套接字
  /// @param ip
  /// @param port
  /// @param non_block
  /// @param listen_backlog
  /// @return
  static int CreateTcpSocket(std::string ip, uint16_t port,
                             bool non_block = true, int listen_backlog = 0) {
    int flags = SOCK_STREAM;
    if (non_block)
      flags |= SOCK_NONBLOCK;

    int fd = socket(AF_INET, flags, 0);
    if (fd == -1)
      return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : inet_addr(ip.c_str());

    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      close(fd);
      return -1;
    }

    if (listen_backlog > 0 && listen(fd, listen_backlog) < 0) {
      close(fd);
      return -1;
    }

    return fd;
  }

  /// @brief 创建UDP套接字
  /// @param ip
  /// @param port
  /// @param non_block
  /// @return
  static int CreateUdpSocket(std::string ip, uint16_t port,
                             bool non_block = true) {
    int flags = SOCK_DGRAM;
    if (non_block)
      flags |= SOCK_NONBLOCK;

    int fd = socket(AF_INET, flags, 0);
    if (fd == -1)
      return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ip.empty() ? INADDR_ANY : inet_addr(ip.c_str());

    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      close(fd);
      return -1;
    }

    return fd;
  }
};
} // namespace net