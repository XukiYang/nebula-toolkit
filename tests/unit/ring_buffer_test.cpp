#include "../../include/containers/ring_buffer.hpp"
#include "../../include/lib/logkit/logkit.hpp"

void General_IO_Testing() {
  LOG_MSG("General_IO_Testing");
  containers::RingBuffer ring_buffer(30);
  std::vector<uint8_t> in = {'h', 'e', 'l', 'l', 'o'}, out;

  // 写入5*5字节
  ring_buffer.Write(in);
  ring_buffer.Write(in);
  ring_buffer.Write(in);
  ring_buffer.Write(in);
  ring_buffer.Write(in);
  ring_buffer.PrintBuffer();

  // 读走5字节，剩余20字节
  ring_buffer.Read(out, in.size());
  ring_buffer.PrintBuffer();

  // 写入10字节
  ring_buffer.Write(in);
  ring_buffer.Write(in);
  ring_buffer.PrintBuffer();

  // 读走10字节
  ring_buffer.Read(out, in.size());
  ring_buffer.Read(out, in.size());
  ring_buffer.PrintBuffer();

  // 可写测试
  containers::RingBuffer ring_buffer_2(1);
  in = {'t'};
  size_t w_ret = ring_buffer_2.Write(in);
  LOGP_MSG("write ret:%d", w_ret);
  w_ret = ring_buffer_2.Write(in);
  LOGP_MSG("write ret:%d", w_ret);

  // 可读测试
  size_t r_ret = ring_buffer_2.Read(in, 2);
  LOGP_MSG("read ret:%d", r_ret);
  r_ret = ring_buffer_2.Read(in, 1);
  LOGP_MSG("read ret:%d", r_ret);
  r_ret = ring_buffer_2.Read(in, 1);
  LOGP_MSG("read ret:%d", r_ret);

  // 环绕读写测试
  containers::RingBuffer ring_buffer_3(5);
  ring_buffer_3.Write({1, 2, 3, 4, 5});
  ring_buffer_3.PrintBuffer();
  ring_buffer_3.Read(out, 3);
  ring_buffer_3.Write({6, 7, 8});
  ring_buffer_3.PrintBuffer();
};

void General_Fullempty_Testing() {
  LOG_MSG("General_Fullempty_Testing");
  containers::RingBuffer ring_buffer(5);
  std::vector<uint8_t> in = {'w', 'o', 'r', 'l', 'd'}, out;

  LOGP_MSG("IsEmpty:%d,IsFull:%d,Usage:%f", ring_buffer.IsEmpty() * 1e2,
           ring_buffer.IsFull(), ring_buffer.Usage());
  ring_buffer.Write(in);
  LOGP_MSG("IsEmpty:%d,IsFull:%d,Usage:%f%", ring_buffer.IsEmpty() * 1e2,
           ring_buffer.IsFull() * 100, ring_buffer.Usage());
  ring_buffer.Read(out, in.size());
  LOGP_MSG("IsEmpty:%d,IsFull:%d,Usage:%f%", ring_buffer.IsEmpty() * 1e2,
           ring_buffer.IsFull() * 100, ring_buffer.Usage());
};

int main(int argc, char const *argv[]) {
  General_IO_Testing();
  General_Fullempty_Testing();
  return 0;
}
