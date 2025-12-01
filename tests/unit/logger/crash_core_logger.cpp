#include "../include/logger/crash_core_logger.hpp"

#include <chrono>
#include <thread>
#include <vector>

void deep_crash_function() {
    // 制造一个段错误
    int* ptr = nullptr;
    // 这里会崩溃
    *ptr = 42;
}

void intermediate_function() {
    std::vector<int> data = {1, 2, 3};
    printf("准备进入崩溃函数...\n");
    deep_crash_function();
}

int main() {
    // 配置崩溃日志记录器
    logger::CrashCoreLogger::getInstance().SetFilePath("crash_dump");
    logger::CrashCoreLogger::getInstance().SetMaxStackDepth(50);
    logger::CrashCoreLogger::getInstance().EnableTimestampFilenames(true);
    printf("程序启动，将在5秒后触发崩溃...\n");
    std::this_thread::sleep_for(std::chrono::seconds(5));
    intermediate_function();
    return 0;
}