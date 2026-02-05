/* Bit位操作工具 */
#pragma once
#include <cstdint>
#include <type_traits>
namespace nebula {
namespace containers {
class BitUtils {
public:
    BitUtils() = delete;

    /* uint32_t [31-0] 方法  */
    static constexpr uint32_t SetBit(uint32_t value, uint8_t pos) {
        return value | (1U << pos);
    }
    static constexpr uint32_t ClearBit(uint32_t value, uint8_t pos) {
        return value & ~(1U << pos);
    }
    static constexpr uint32_t ToggleBit(uint32_t value, uint8_t pos) {
        return value ^ (1U << pos);
    }
    static constexpr bool TestBit(uint32_t value, uint8_t pos) {
        return (value >> pos) & 1U;
    }
    static constexpr uint32_t GetBits(uint32_t value, uint8_t start, uint8_t end) {
        return (value >> start) & ((end - start + 1) == 32 ? 0xFFFFFFFF : ((1U << (end - start + 1)) - 1));
    }
    static constexpr uint32_t SetBits(uint32_t value, uint32_t bits, uint8_t start, uint8_t end) {
        return (value & ~(((end - start + 1) == 32 ? 0xFFFFFFFF : ((1U << (end - start + 1)) - 1)) << start))
               | ((bits & ((end - start + 1) == 32 ? 0xFFFFFFFF : ((1U << (end - start + 1)) - 1))) << start);
    }

    /* uint64_t [63-0] 方法 */
    static constexpr uint64_t SetBit(uint64_t value, uint8_t pos) {
        return value | (1ULL << pos);
    }
    static constexpr uint64_t ClearBit(uint64_t value, uint8_t pos) {
        return value & ~(1ULL << pos);
    }
    static constexpr uint64_t ToggleBit(uint64_t value, uint8_t pos) {
        return value ^ (1ULL << pos);
    }
    static constexpr bool TestBit(uint64_t value, uint8_t pos) {
        return (value >> pos) & 1ULL;
    }
    static constexpr uint64_t GetBits(uint64_t value, uint8_t start, uint8_t end) {
        return (value >> start) & ((end - start + 1) == 64 ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << (end - start + 1)) - 1));
    }
    static constexpr uint64_t SetBits(uint64_t value, uint64_t bits, uint8_t start, uint8_t end) {
        return (value
                & ~(((end - start + 1) == 64 ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << (end - start + 1)) - 1)) << start))
               | ((bits & ((end - start + 1) == 64 ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << (end - start + 1)) - 1)))
                  << start);
    }

    /* 高级操作 */
    static constexpr uint8_t PopulationCount(uint32_t value) {
        return (value == 0) ? 0 : ((value & 1) + PopulationCount(value >> 1));
    }
    static constexpr uint8_t PopulationCount(uint64_t value) {
        return (value == 0) ? 0 : ((value & 1) + PopulationCount(value >> 1));
    }
    static constexpr uint8_t FindFirstSet(uint32_t value) {
        return (value == 0) ? 32 : ((value & 1) ? 0 : (1 + FindFirstSet(value >> 1)));
    }
    static constexpr uint8_t FindFirstSet(uint64_t value) {
        return (value == 0) ? 64 : ((value & 1) ? 0 : (1 + FindFirstSet(value >> 1)));
    }
};

}  // namespace containers
}  // namespace nebula