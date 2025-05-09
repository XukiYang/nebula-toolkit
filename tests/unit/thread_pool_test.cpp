/*
 * @Descripttion:
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-05-09 15:06:38
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-05-09 17:00:04
 */
#include "../../include/lib/logkit/logkit.hpp"
#include "../../include/lib/threading/thread_pool.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <string>

int main() {

  // 演示用例
  LOGP_MSG("=== 演示用例 ===");
  ThreadPool demo_pool(3);

  demo_pool.PostTask([]() -> size_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOGP_MSG("任务1完成");
    return 1;
  });

  demo_pool.PostTask([]() -> size_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    LOGP_MSG("任务2完成");
    return 2;
  });

  demo_pool.PostTask([]() -> size_t {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    LOGP_MSG("任务3完成");
    return 3;
  });

  // 等待演示任务完成
  std::this_thread::sleep_for(std::chrono::seconds(1));
  return 0;
}