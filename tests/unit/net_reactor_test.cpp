#include "net/core/reactor_core.hpp"
#include "net/transport/socket_creator.hpp"
#include <csignal>
#include <iostream>

std::atomic<bool> running{true};

void signalHandler(int signal) {
  if (signal == SIGINT) {
    running = false;
  }
}

int main() {
  using namespace net;

  // 注册信号处理
  signal(SIGINT, signalHandler);

  ReactorCore reactor;

  // 创建TCP服务器套接字
  int tcp_fd = SocketCreator::CreateTcpSocket("0.0.0.0", 8080, true, SOMAXCONN);
  if (tcp_fd < 0) {
    std::cerr << "Failed to create TCP socket\n";
    return 1;
  }

  // 注册TCP监听套接字
  reactor.RegisterProtocol(tcp_fd, nullptr, TriggerMode::kEt, true);
  reactor.SetConnHandlerParams(
      {0xE, 0xD}, {0xA}, nullptr, nullptr,
      [](std::vector<std::vector<uint8_t>> &packs) -> void {
        for (const auto &pack : packs) {
          std::cout << "Received TCP packet: ";
          for (const auto &byte : pack) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
          }
          std::cout << std::dec << "\n";
        }
      });

  // // 创建UDP套接字
  // int udp_fd = SocketCreator::CreateUdpSocket("0.0.0.0", 9090);
  // if (udp_fd < 0) {
  //   std::cerr << "Failed to create UDP socket\n";
  //   return 1;
  // }

  // // 为UDP创建处理器
  // auto udp_handler = []() -> std::unique_ptr<ProtocolHandler> {
  //   return nullptr;
  // };

  // reactor.RegisterProtocol(udp_fd, udp_handler(), TriggerMode::kEt);

  std::cout << "Server started. Listening on TCP:8080 and UDP:9090\n";
  std::cout << "Press Ctrl+C to exit...\n";

  // 运行事件循环
  reactor.Run();

  return 0;
}