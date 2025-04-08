#include "../include/containers/ring_buffer.hpp"
#include <iostream>

int main() {
  containers::RingBuffer ring_buffer(4);
  std::vector<uint8_t> test_write_vec = {0, 1, 2};
  ring_buffer.WriteData(test_write_vec);
  
  std::vector<uint8_t> test_read_vec;
  ring_buffer.ReadData(&test_read_vec, 3);
  for (const auto &item : test_read_vec) {
    std::cout << std::to_string(item) << std::endl;
  }
  return 0;
};