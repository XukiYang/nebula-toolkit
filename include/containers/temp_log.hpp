/*
 * @Descripttion:
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-04-08 14:16:20
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-04-08 14:29:23
 */

#include <iostream>
#include <sstream>
#include <string.h>
#include <string>
#include <utility>

template <typename... Args> void LOG(const Args &...args) {
  ((std::cout << args << " "), ...);
  std::cout << std::endl;
}

template <typename T> void LOG(const std::vector<T> args) {
  for (const auto &item : args) {
    std::cout << std::to_string(item);
  }
  std::cout << std::endl;
}

// template <typename... Args> void LOGF(const char *format, const Args
// &...args) {
//   int size = snprintf(nullptr, 0, format, args...) + 1;
//   if (size <= 0)
//     return;
//   auto buf = std::make_unique<char[]>(size);
//   snprintf(buf.get(), size, format, args...);
//   std::cout << buf.get() << std::endl;
// }