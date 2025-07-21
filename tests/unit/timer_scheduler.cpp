#include "../../include/threading/timer_scheduler.hpp"
#include "../../include/logger/logger.hpp"
#include <unistd.h>

int main() {
  TimerScheduler timer(2); // 2个线程
  timer.Start();
  LOG_MSG("Start");
  // 基本测试
  timer.ScheduleOnce(1000, []() {
    LOG_MSG("1秒后执行");
    return 0;
  });
  timer.ScheduleOnce(2000, []() {
    LOG_MSG("2秒后执行");
    return 0;
  });

  // 取消测试
  auto cancel_id = timer.ScheduleOnce(3000, []() {
    LOG_MSG("这个应该看不到");
    return 0;
  });
  timer.Cancel(cancel_id);

  // 保持运行
  sleep(10);
  timer.Stop();
  LOG_MSG("测试结束");
}