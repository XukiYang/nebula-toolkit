#include <csignal>
#include <iostream>

#include "net/core/reactor_core.hpp"
#include "net/transport/socket_creator.hpp"
#include "threading/timer_scheduler.hpp"

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

    // 定义回调函数
    auto exec_cb = [](const std::vector<std::vector<uint8_t>>& packs) -> void {
        // for (const auto& vec : packs) {
        // fmt::print("PackData");
        // fmt::print("{}", fmt::join(vec, " "));
        // fmt::print("\n\n");
        // }

        fmt::println("PasingDataPacket {}", packs.size());
    };

    // 注册TCP监听套接字
    reactor.RegisterProtocol(tcp_fd, nullptr, true);
    reactor.SetConnHandlerParams({0xE, 0xD, 0xF},  // head_key
                                 {0xA, 0xE},       // tail_key
                                 nullptr,          // data_sz_cb
                                 nullptr,          // check_sz_cb
                                 exec_cb,          // exec_cb
                                 8192              // buffer_size
    );
    std::cout << "Server started. Listening on TCP:8080\n";
    std::cout << "Press Ctrl+C to exit...\n";

    // 运行事件循环
    reactor.Run();

    return 0;
}
