#include "../../include/net/core/reactor_core.hpp"
#include <iostream>

int main(int argc, char const *argv[]) {
  using namespace net;
  ReactorCore react_core;

  int server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // 设置地址重用SO_REUSEADDR选项（避免TIME_WAIT状态导致绑定失败）
  int opt = 1;
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  // 配置服务器地址
  struct sockaddr_in server_addr_;
  server_addr_.sin_family = AF_INET;
  server_addr_.sin_addr.s_addr = INADDR_ANY;
  server_addr_.sin_port = htons(8089);

  // 绑定套接字到指定端口
  if (bind(server_fd_, (struct sockaddr *)&server_addr_, sizeof(server_addr_)) <
      0) {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd_, SOMAXCONN) == -1) {
    perror("listen");
    close(server_fd_);
    exit(EXIT_FAILURE);
  }
  printf("Server listening on port %d...\n", 8089);

  auto un_packer = std::make_unique<containers::UnPacker>(
      containers::HeadKey{0xE}, containers::TailKey{}, 1024);

  auto handler = std::make_unique<TcpHandler>(un_packer.get());

  react_core.RegisterProtocol(server_fd_, std::move(handler));
  react_core.Run();
  return 0;
}
