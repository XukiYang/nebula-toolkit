#include "../../include/containers/byte_stream.hpp"
#include "../../include/logger/logger.hpp"

#pragma pack(1)
struct TestStruct {
  uint16_t age;
  uint8_t sex;
};
#pragma

void General_IO_Testing() {
  containers::ByteStream byte_stream(30);

  // 测试自定义类型IO
  TestStruct test_in_struct, test_out_struct;
  byte_stream.PrintBuffer();

  test_in_struct.age = 18;
  test_in_struct.sex = 1;

  byte_stream << test_in_struct;
  byte_stream.PrintBuffer();

  byte_stream >> test_out_struct;
  byte_stream.PrintBuffer();
  LOGP_MSG("%d %d", test_out_struct.age, test_out_struct.sex);

  std::vector<int> test_in_vector = {1, 2, 3};
  std::vector<int> test_out_vector(3);
  LOG_VECTOR(test_out_vector);

  byte_stream << test_in_vector;
  byte_stream.PrintBuffer();
  byte_stream >> test_out_vector;
  LOG_VECTOR(test_out_vector);
  LOGP_MSG("%d,%d", test_in_vector.size(), test_out_vector.size());

  std::string test_in_string("hello"), test_out_string;
  byte_stream << test_in_string;
  byte_stream.PrintBuffer();

  test_out_string.resize(5);
  byte_stream >> test_out_string;
  LOG_MSG(test_out_string);
}

int main(int argc, char const *argv[]) {
  General_IO_Testing();
  return 0;
}
