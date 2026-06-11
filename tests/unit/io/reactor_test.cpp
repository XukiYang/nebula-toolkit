#include <csignal>
#include <iostream>

#include <fmt/ranges.h>

#include "logger/crash_core_logger.hpp"
#include "io/core/reactor_core.hpp"
#include "io/transport/socket_creator.hpp"
#include "threading/timer_scheduler.hpp"

std::atomic<bool> running{true};

int main() {
    nebula::logger::CrashCoreLogger::Init("crash_dump", {.max_stack_depth = 50, .use_timestamp_filename = true});

    using namespace nebula;
    io::core::ReactorCore reactor;

    // 定时线程池依赖注入
    auto timer_scheduler = std::make_shared<threading::TimerScheduler>();
    reactor.SetTimerScheduler(std::move(timer_scheduler));

    // 定义回调函数
    // ExecCb 签名: void(int fd, const std::vector<std::vector<uint8_t>>& packs)
    size_t               pack_count  = 0;
    size_t               error_count = 0;
    size_t               null_count  = 0;
    std::vector<uint8_t> com_pack    = {0xE, 0xD, 0xF, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xE};
    auto                 exec_cb     = [&pack_count, &error_count, &null_count,
                    &com_pack](int fd, const std::vector<std::vector<uint8_t>>& packs) -> void {
        for (const auto& vec : packs) {
            fmt::print("[fd:{}] PackData [{}-{}-{}]:", fd, pack_count, null_count, error_count);
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

    // 先设置解包参数，再通过 ListenTcp 创建监听（内部会创建 TcpListenerHandler）
    reactor.SetConnHandlerParams({0xE, 0xD, 0xF},  // head_key
                                 {0xA, 0xE},       // tail_key
                                 nullptr,           // data_sz_cb
                                 nullptr,           // check_sz_cb
                                 exec_cb,           // exec_cb
                                 1024 * 16          // buffer_size
    );

    int tcp_fd = reactor.ListenTcp("0.0.0.0", 8080, SOMAXCONN);
    if (tcp_fd < 0) {
        std::cerr << "Failed to create TCP socket\n";
        return 1;
    }
    std::cout << "Server started. Listening on TCP:8080\n";
    std::cout << "Press Ctrl+C to exit...\n";

    // 运行事件循环
    reactor.Run();

    return 0;
}
