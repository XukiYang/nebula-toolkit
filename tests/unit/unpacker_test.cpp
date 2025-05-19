#include "../../include/containers/unpacker.hpp"

int main(int argc, char const *argv[]) {
  using namespace containers;

  // 测试包含头与尾定位符的解包
  // UnPacker up(HeadSzCb([] { return 0; }), TailSzCb([] { return 0; }),
  //             HeadKey{0x7, 0x9}, TailKey{0xE}, 128);

  // 测试仅包含头定位符的解包
  UnPacker up(HeadSzCb([] { return 0; }), TailSzCb([] { return 0; }),
              HeadKey{0x7, 0x9}, HeadKey{}, 128);

  std::vector<uint8_t> test_in_data = {0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8, 0xE,
                                       0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8, 0xE,
                                       0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8, 0xE,
                                       0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8, 0xE};

  std::vector<uint8_t> test_in_data_2 = {
      0x7, 0x9, 66, 77, 88, 0xE, 0x7, 0x9, 66, 77, 88, 0xE,
      0x7, 0x9, 66, 77, 88, 0xE, 0x7, 0x9, 66, 77, 88, 0xE};

  std::vector<std::vector<uint8_t>> test_out_data;

  up.PushAndGet(test_in_data.data(), test_in_data.size(), test_out_data);
  LOGP_MSG("%d", up.Length());

  up.PushAndGet(test_in_data_2.data(), test_in_data_2.size(), test_out_data);
  LOGP_MSG("%d", up.Length());

  for (const auto &item : test_out_data) {
    LOG_VECTOR(item);
  }
  return 0;
}
