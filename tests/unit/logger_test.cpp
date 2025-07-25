#include "../include/logger/logger.hpp"
#include <chrono>
#include <fmt/chrono.h>
#include <fmt/core.h>

std::string FormatDuration(uint64_t ns) {
  if (ns < 1'000) {
    return fmt::format("{:>6} ns", ns);
  } else if (ns < 1'000'000) {
    return fmt::format("{:>6.3f} μs", ns / 1e3);
  } else if (ns < 1'000'000'000) {
    return fmt::format("{:>6.3f} ms", ns / 1e6);
  } else {
    return fmt::format("{:>6.3f} s", ns / 1e9);
  }
}

int main() {
  constexpr size_t kTestCount = 50000;

  fmt::print("预热运行中...");
  for (size_t i = 0; i < 10; ++i) {
    LOGP_DEBUG("Warmup log %zu", i);
  }

  fmt::print("开始性能测试 ({} 次日志)...\n", kTestCount);
  auto start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < kTestCount; ++i) {
    LOG_INFO("Test log index:", i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  auto avg_ns = duration.count() / kTestCount;

  fmt::print("├────────────────────────────────────────┤\n");
  fmt::print("│ {:20} │ {:>15} │\n", "测试总量", kTestCount);
  fmt::print("├──────────────────────┼─────────────────┤\n");
  fmt::print("│ {:20} │ {:>15} │\n", "总用时",
             FormatDuration(duration.count()));
  fmt::print("│ {:20} │ {:>15} │\n", "单次日志耗时", FormatDuration(avg_ns));
  fmt::print("│ {:20} │ {:>7.1f} logs/ms │\n", "吞吐量",
             kTestCount / (duration.count() / 1e6));
  fmt::print("└──────────────────────┴─────────────────┘\n");
  return 0;
}