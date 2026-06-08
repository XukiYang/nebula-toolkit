#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nebula {
namespace crypto {

class Crc32 {
private:
    static constexpr uint32_t POLYNOMIAL = 0xEDB88320;
    static constexpr uint32_t INITIAL_VALUE = 0xFFFFFFFF;
    static constexpr uint32_t FINAL_XOR_VALUE = 0xFFFFFFFF;

    static const uint32_t* GetTable() {
        static uint32_t table[256];
        static bool initialized = false;

        if (!initialized) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t crc = i;
                for (int j = 0; j < 8; j++) {
                    if (crc & 1) {
                        crc = (crc >> 1) ^ POLYNOMIAL;
                    } else {
                        crc >>= 1;
                    }
                }
                table[i] = crc;
            }
            initialized = true;
        }
        return table;
    }

    uint32_t crc_value_;

public:
    Crc32() : crc_value_(INITIAL_VALUE) {}

    void Reset() {
        crc_value_ = INITIAL_VALUE;
    }

    void Update(const uint8_t* data, size_t length) {
        if (!data || length == 0) return;

        const auto& table = GetTable();
        for (size_t i = 0; i < length; i++) {
            uint8_t index = (crc_value_ ^ data[i]) & 0xFF;
            crc_value_ = (crc_value_ >> 8) ^ table[index];
        }
    }

    void Update(const std::vector<uint8_t>& data) {
        Update(data.data(), data.size());
    }

    void Update(const std::string& data) {
        Update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    uint32_t GetValue() const {
        return crc_value_ ^ FINAL_XOR_VALUE;
    }

    static uint32_t Calculate(const uint8_t* data, size_t length) {
        if (!data || length == 0) return 0;

        Crc32 crc;
        crc.Update(data, length);
        return crc.GetValue();
    }

    static uint32_t Calculate(const std::vector<uint8_t>& data) {
        return Calculate(data.data(), data.size());
    }

    static uint32_t Calculate(const std::string& data) {
        return Calculate(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }

    static uint32_t Calculate(const void* data, size_t size) {
        return Calculate(static_cast<const uint8_t*>(data), size);
    }

    static bool Verify(const uint8_t* data, size_t length, uint32_t expected_crc) {
        return Calculate(data, length) == expected_crc;
    }

    static bool Verify(const std::vector<uint8_t>& data, uint32_t expected_crc) {
        return Verify(data.data(), data.size(), expected_crc);
    }

    static bool Verify(const std::string& data, uint32_t expected_crc) {
        return Verify(reinterpret_cast<const uint8_t*>(data.data()), data.size(), expected_crc);
    }

    static bool Verify(const void* data, size_t size, uint32_t expected_crc) {
        return Verify(static_cast<const uint8_t*>(data), size, expected_crc);
    }
};

uint32_t GenerateChecksum(const void* data, size_t size) {
    return Crc32::Calculate(data, size);
}

bool VerifyChecksum(const void* data, size_t size, uint32_t checksum) {
    return Crc32::Verify(data, size, checksum);
}

}  // namespace crypto
}  // namespace nebula