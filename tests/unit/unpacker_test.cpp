#include "../../include/containers/unpacker.hpp"

int main(int argc, char const *argv[]) {
  using namespace containers;

  UnPacker up(HeadSzCb([] { return 0; }), TailSzCb([] { return 0; }),
              HeadKey{0x7, 0x9}, TailKey{}, 128);

  std::vector<uint8_t> test_in_data = {0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8,
                                       0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8,
                                       0x7, 0x9, 1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<std::vector<uint8_t>> test_out_data;

  up.PushAndGet(test_in_data.data(), test_in_data.size(), test_out_data);

  LOGP_INFO("test_out_data_size:%d,up.Length():%d", test_out_data.size(),
            up.Length());
  for (const auto &item : test_out_data) {
    LOG_VECTOR(item);
  }
  return 0;
}
