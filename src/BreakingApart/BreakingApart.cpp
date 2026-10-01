//
// Created by ADGaiNai6657 on 2026/9/30.
//

#include "BreakingApart.h"
#include "GearHash/GearHash.h"

#include <algorithm>

constexpr std::size_t TARGET_CHUNK_SIZE  = 8096;
constexpr std::size_t MINIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE / 2;
constexpr std::size_t MAXIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE * 8;
constexpr std::uint64_t MASK_S = 0x0003590703530000ULL; // 15 of ones; Harder to hit, should be put in the big para. WITH NC
constexpr std::uint64_t MASK_M = 0x0000d90303530000ULL; // 13 of ones; Middium, put in middle. WITHOUT NC
constexpr std::uint64_t MASK_L = 0x0000d90003530000ULL; // 11 of ones; Easier to hit, put in small para. WITH NC

// initialize the para. list.
namespace {
    BreakingApartConfig breakingApartConfig{
        MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MASK_S, MASK_M, MASK_L
    };

    // 依据当前块内偏移选取掩码：未启用 NC 统一用 MaskM；
    // 启用 NC 时，NormalSize 之前用较难命中的 MaskS，之后用较易命中的 MaskL。
    auto maskFor(std::size_t chunkOffset, bool isNC) -> std::uint64_t {
        if (!isNC) {
            return breakingApartConfig.MaskM;
        }
        return (chunkOffset < breakingApartConfig.NormalSize) ? breakingApartConfig.MaskS
                                                              : breakingApartConfig.MaskL;
    }
}

auto BoundariesFinder(std::string_view data,
                      std::vector<std::vector<size_t>> &vPosition,
                      bool is_NC)
-> void {

    // 将此二维向量向后添加一个空向量准备加入 cut-point 的下标信息
    vPosition.emplace_back();
    auto &cuts = vPosition.back();

    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }

    const auto *bytes = reinterpret_cast<const std::uint8_t *>(data.data());

    std::size_t last = 0; // 当前块的起始下标（上一次的 cut-point）
    while (last < n) {
        // 剩余数据不足 MinSize，直接作为最后一块切到末尾
        if (n - last <= breakingApartConfig.MinSize) {
            cuts.push_back(n);
            break;
        }

        // 查找窗口不能超过 MaxSize，也不能越过数据末尾
        const std::size_t searchEnd = std::min(last + breakingApartConfig.MaxSize, n);

        std::uint64_t hash = 0;
        std::size_t cut = searchEnd; // 始终未命中掩码则在 MaxSize 处强制切分

        // 跳过 MinSize 区间；此后每滚入一字节判断一次掩码
        for (std::size_t i = last + breakingApartConfig.MinSize; i < searchEnd; ++i) {
            hash = gearRoll(hash, bytes[i]);
            if ((hash & maskFor(i - last, is_NC)) == 0) {
                cut = i;
                break;
            }
        }

        cuts.push_back(cut);
        last = cut;
    }
}
