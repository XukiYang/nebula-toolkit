// /*
//  * @Descripttion:
//  * @version: 1.0
//  * @Author: YangHouQi
//  * @Date: 2025-04-08 15:42:37
//  * @LastEditors: YangHouQi
//  * @LastEditTime: 2025-04-08 15:44:38
//  */
// #include "../../include/containers/byte_stream.hpp"

// containers::ByteStream::ByteStream(size_t buffer_size)
//     : ring_buffer_(std::make_unique<RingBuffer>(buffer_size)) {}

// int containers::ByteStream::WriteRaw(const void *data, size_t size) {
//   return ring_buffer_->WriteData({static_cast<const uint8_t *>(data),
//                                   static_cast<const uint8_t *>(data) + size});
// }

// int containers::ByteStream::ReadRaw(void *data, size_t size) {
//   std::vector<uint8_t> buffer(size);
//   int result = ring_buffer_->ReadData(&buffer, size);
//   if (result > 0) {
//     memcpy(data, buffer.data(), result);
//   }
//   return result;
// }

// containers::ByteStream &
// containers::ByteStream::operator<<(const std::string &data) {
//   WriteRaw(data.data(), data.size());
//   return *this;
// }

// containers::ByteStream &containers::ByteStream::operator>>(std::string &data) {
//   ReadRaw(data.data(), data.size());
//   return *this;
// }