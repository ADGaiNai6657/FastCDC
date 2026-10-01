//
// Created by user on 2026/9/30.
//

#include "GearHash.h"

// 将 length 个字节依次滚入 hash；hash 由调用者持有，便于跨调用累加
auto updateGearHash(const std::uint8_t *data, std::size_t length, std::uint64_t &hash) -> std::uint64_t {
    for (std::size_t i = 0; i < length; ++i) {
        hash = gearRoll(hash, data[i]);
    }
    return hash;
}

