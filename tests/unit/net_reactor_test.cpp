#include "net/core/reactor_core.hpp"
#include "net/transport/socket_creator.hpp"
#include "threading/timer_scheduler.hpp"
#include <csignal>
#include <iostream>

std::atomic<bool> running{true};

// 注释便于调试
// void signalHandler(int signal) {
//   if (signal == SIGINT) {
//     running = false;
//   }
// }
// 注册信号处理
// signal(SIGINT, signalHandler);

int main() {
  using namespace net;

  ReactorCore reactor;

  // 定时线程池依赖注入
  auto timer_scheduler = std::make_shared<threading::TimerScheduler>();
  reactor.SetTimerScheduler(std::move(timer_scheduler));

  // 创建TCP服务器套接字
  int tcp_fd = SocketCreator::CreateTcpSocket("0.0.0.0", 8080, true, SOMAXCONN);
  if (tcp_fd < 0) {
    std::cerr << "Failed to create TCP socket\n";
    return 1;
  }

  // 注册TCP监听套接字
  reactor.RegisterProtocol(tcp_fd, nullptr, true);
  reactor.SetConnHandlerParams(
      {0xE, 0xD}, {0xA}, nullptr, nullptr,
      [](std::vector<std::vector<uint8_t>> &packs) -> void {
        for (const auto &pack : packs) {
          LOG_VECTOR(pack);
        }
      });

  // 创建UDP套接字
  int udp_fd = SocketCreator::CreateUdpSocket("0.0.0.0", 9090);
  if (udp_fd < 0) {
    std::cerr << "Failed to create UDP socket\n";
    return 1;
  }

  // 为UDP创建处理器
  auto udp_unpacker = containers::UnPacker::CreateBasic(
      containers::HeadKey{0xE, 0xD}, containers::TailKey{0xA}, 2048);
  auto udp_handler =
      std::make_unique<UdpHandler>(udp_fd, std::move(udp_unpacker));
  udp_handler->SetCallback([](std::vector<std::vector<uint8_t>> &packs) {
    for (const auto &pack : packs) {
      LOG_VECTOR(pack);
    }
  });

  reactor.RegisterProtocol(udp_fd, std::move(udp_handler));

  std::cout << "Server started. Listening on TCP:8080 and UDP:9090\n";
  std::cout << "Press Ctrl+C to exit...\n";

  // 运行事件循环
  reactor.Run();

  return 0;
}