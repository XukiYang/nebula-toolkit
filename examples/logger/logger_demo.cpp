// =============================================================================
// logger_demo.cpp -- 日志系统教学示例
// =============================================================================
//
// 核心思想:
//   nebula 日志系统是异步日志，使用双缓冲 + 消费线程实现高性能:
//   1. 业务线程将日志写入缓冲区（CircularBuffer 或 LockFreeQueue）
//   2. 消费线程从缓冲区读取，批量写入文件
//   3. 日志文件按大小自动轮转，文件名带日期时间戳
//
// 日志级别:
//   MSG   -- 仅输出到 stdout，不写文件
//   INFO  -- 输出到 stdout + 文件
//   WARN  -- 输出到 stdout + 文件
//   DEBUG -- 输出到 stdout + 文件
//   ERROR -- 输出到 stdout + 文件
//
// 注意: Logger 是单例，配置文件路径为 ./configs/log_config.json
//       如果配置文件不存在，会使用默认配置
//
// =============================================================================

#include <fmt/format.h>
#include <logger/logger.hpp>

#include <thread>
#include <vector>

int main() {
    fmt::println("========================================");
    fmt::println("  日志系统教学示例");
    fmt::println("========================================\n");

    // 1. 基本日志宏: LOG_MSG / LOG_INFO / LOG_WARN / LOG_ERROR
    //    使用 iostream 风格 << 操作符
    //    宏会自动捕获 __func__ 和 __LINE__
    fmt::println("--- 1. 基本日志宏 (iostream 风格) ---");
    LOG_MSG("这是 MSG 级别日志 (仅 stdout)");
    LOG_INFO("这是 INFO 级别日志");
    LOG_WARN("这是 WARN 级别日志");
    LOG_ERROR("这是 ERROR 级别日志");

    // 带变量的日志
    int code = 404;
    std::string msg = "Not Found";
    LOG_INFO("HTTP 错误: code=", code, ", msg=", msg);

    // 2. fmt 格式化日志: LOGF_INFO 等
    //    使用 fmt 格式化语法，更灵活
    fmt::println("\n--- 2. fmt 格式化日志 ---");
    LOGF_INFO("fmt格式化: {} + {} = {}", 3, 4, 7);
    LOGF_WARN("警告: 剩余 {}% 空间", 15);
    LOGF_ERROR("错误: 文件 \"{}\" 不存在", "config.json");

    // 3. 向量日志: LOG_VECTOR
    //    自动打印 vector 内容，uint8_t 会显示为整数
    fmt::println("\n--- 3. 向量日志 ---");
    std::vector<int> data = {1, 2, 3, 4, 5};
    LOG_VECTOR(data);

    std::vector<uint8_t> bytes = {0xDE, 0xAD, 0xBE, 0xEF};
    LOG_VECTOR(bytes);

    // 4. 多线程日志
    //    异步日志天然支持多线程，业务线程不阻塞在 IO 上
    fmt::println("\n--- 4. 多线程日志 ---");
    std::vector<std::thread> threads;
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 3; ++j) {
                LOGF_INFO("线程 {} 日志 #{}", i, j);
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    fmt::println("  多线程日志写入完成");

    // 5. CrashCoreLogger 信号安全设计 (注释讲解)
    //    CrashCoreLogger 是崩溃时的最后防线:
    //    - 拦截 SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS 等致命信号
    //    - 信号处理函数中只使用 async-signal-safe 的系统调用:
    //      write()、open()、close()、backtrace()、backtrace_symbols_fd()
    //    - 不使用 malloc/free/new/delete/mutex 等非信号安全函数
    //    - 输出包含: 时间戳、信号信息、PID/PPID/UID、栈回溯
    //    - 写完崩溃日志后重新 raise 信号，让系统生成 core dump
    fmt::println("\n--- 5. CrashCoreLogger 信号安全设计 ---");
    fmt::println("  CrashCoreLogger 拦截致命信号并写入崩溃日志");
    fmt::println("  信号处理函数中仅使用 async-signal-safe 调用:");
    fmt::println("    write() / open() / close() / backtrace() / backtrace_symbols_fd()");
    fmt::println("  不使用 malloc/free/new/delete/mutex 等非安全函数");

    // 初始化 CrashCoreLogger (可选，实际使用时取消注释)
    // nebula::logger::CrashCoreLogger::Init("./crash_dump");
    fmt::println("  (初始化代码已注释，取消注释以启用)");

    fmt::println("\n========================================");
    fmt::println("  日志系统示例结束");
    fmt::println("========================================");

    return 0;
}
