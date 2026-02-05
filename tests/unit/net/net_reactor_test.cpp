#include <csignal>
#include <iostream>

#include "../include/logger/crash_core_logger.hpp"
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
    nebula::logger::CrashCoreLogger::getInstance().SetFilePath("crash_dump");
    nebula::logger::CrashCoreLogger::getInstance().SetMaxStackDepth(50);
    nebula::logger::CrashCoreLogger::getInstance().EnableTimestampFilenames(true);

    using namespace nebula;
    net::core::ReactorCore reactor;

    // 定时线程池依赖注入
    auto timer_scheduler = std::make_shared<threading::TimerScheduler>();
    reactor.SetTimerScheduler(std::move(timer_scheduler));

    // 创建TCP服务器套接字
    int tcp_fd = net::transport::SocketCreator::CreateTcpSocket("0.0.0.0", 8080, true, SOMAXCONN);
    if (tcp_fd < 0) {
        std::cerr << "Failed to create TCP socket\n";
        return 1;
    }

    // 定义回调函数
    size_t               pack_count  = 0;
    size_t               error_count = 0;
    size_t               null_count  = 0;
    std::vector<uint8_t> com_pack    = {0xE, 0xD, 0xF, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xE};
    auto                 exec_cb     = [&pack_count, &error_count, &null_count,
                    &com_pack](const std::vector<std::vector<uint8_t>>& packs) -> void {
        for (const auto& vec : packs) {
            fmt::print("PackData [{}-{}-{}]:", pack_count, null_count, error_count);
            fmt::print("{}", fmt::join(vec, " "));
            fmt::print("\n\n");

            if (vec.empty()) {
                null_count++;
            }

            if (vec != com_pack) {
                error_count++;
            }
            pack_count++;
        }
    };

    // 注册TCP监听套接字
    reactor.RegisterProtocol(tcp_fd, nullptr, true);
    reactor.SetConnHandlerParams({0xE, 0xD, 0xF},  // head_key
                                 {0xA, 0xE},       // tail_key
                                 nullptr,          // data_sz_cb
                                 nullptr,          // check_sz_cb
                                 exec_cb,          // exec_cb
                                 1024 * 16         // buffer_size
    );
    std::cout << "Server started. Listening on TCP:8080\n";
    std::cout << "Press Ctrl+C to exit...\n";

    // 运行事件循环
    reactor.Run();

    return 0;
}
