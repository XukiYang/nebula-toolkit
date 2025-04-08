/*
 * @Descripttion:
 * @version: 1.0
 * @Author: YangHouQi
 * @Date: 2025-04-07 14:57:20
 * @LastEditors: YangHouQi
 * @LastEditTime: 2025-04-08 15:35:49
 */
#include "../include/containers/byte_stream.hpp"
#include "../include/containers/ring_buffer.hpp"
#include "../include/containers/temp_log.hpp"
#include <iostream>

int test_ring_buffer() {
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

struct TestStruct {
  uint8_t age;
  uint8_t sex;
};

int main() {
  TestStruct test_struct_sender{80, 3};
  containers::ByteStream byte_stream(50);

  // test input
  byte_stream << test_struct_sender;
  byte_stream.PrintBuffer();

  // test output
  TestStruct test_struct_recver;
  byte_stream >> test_struct_recver;
  byte_stream.PrintBuffer();
  printf("%d %d\n", test_struct_recver.age, test_struct_recver.sex);

  // test output too
  TestStruct test_struct_recver2;
  byte_stream >> test_struct_recver2;
  byte_stream.PrintBuffer();
  printf("%d %d\n", test_struct_recver2.age, test_struct_recver2.sex);

  // test input string
  byte_stream << std::string("hello world");
  byte_stream.PrintBuffer();

  // test input vector
  std::vector<int> test_bool_vector = {81, 82, 83};
  byte_stream << test_bool_vector;
  byte_stream.PrintBuffer();

  // test output string
  std::string test_string_recver;
  test_string_recver.resize(11);
  byte_stream >> test_string_recver;
  LOG(test_string_recver);

  // test output vecor
  std::vector<int> test_bool_vector_recver(3);
  byte_stream >> test_bool_vector_recver;
  LOG(test_bool_vector_recver);
  return 0;
};