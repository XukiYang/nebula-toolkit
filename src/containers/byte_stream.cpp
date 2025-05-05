#include "../../include/containers/byte_stream.hpp"

containers::ByteStream &
containers::ByteStream::operator<<(const std::string &data) {
  Write(reinterpret_cast<const std::byte *>(data.data()), data.size());
  return *this;
}
containers::ByteStream &containers::ByteStream::operator>>(std::string &data) {
  Read(reinterpret_cast<std::byte *>(data.data()), data.size());
  return *this;
};