#include "../include/logger/logger.hpp"

int main(int argc, char const *argv[]) {
  int i = 0;
  while (true) {
    LOGP_DEBUG("cur:%d", i);
    i++;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}
