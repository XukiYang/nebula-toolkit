#include "../../include/containers/unpacker.hpp"
#include <chrono>
#include <random>

int main(int argc, char const *argv[]) {
  using namespace containers;

  UnPacker up(
      HeadKey{0x7, 0x9}, HeadKey{0xE,0XD},
      [](const uint8_t *head_ptr, size_t &head_size, size_t &data_size,
         size_t &tail_size) {
        head_size = 3;
        data_size = head_ptr[2];
        tail_size = 2;
      },
      [](const uint8_t *data_ptr) -> bool { return true; }, 1024);

  std::vector<uint8_t> test_in_data = {
      0x1,0x2,0x3,  // 鲁棒
      0x7,0x9,8,1,2,3,4,5,6,7,8,0xE,0XD,
      0x7,0x9,8,1,2,3,4,5,6,7,8,0xE,0XD,
      0x7,0x9,8,1,2,3,4,5,6,7,8,0xE,0XD,
      0x7,0x9,8,1,2,3,4,5,6,7,8,0xE,0XD,
      0x7,0x9,3,0xA,0xB,0xC,0xE,0XD,
      0x7,0x9,3,0xA,0xB,0xC,0xE,0XD,
      0x7,0x9,3,0xA,0xB,0xC,0xE,0XD,
      0x4,0x5,0x6, // 鲁棒
    };

  std::vector<std::vector<uint8_t>> test_out_data;

  up.PushAndGet(test_in_data.data(), test_in_data.size(), test_out_data);
  LOGP_MSG("剩余%d可读字节,解出%d包", up.Length(),test_out_data.size());

  for (const auto &item : test_out_data) {
    LOG_VECTOR(item);
  }
  return 0;
}
