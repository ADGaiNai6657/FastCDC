//
// Baselines.cpp
// 基线分块实现。三种算法共用「min 跳过 + max 强制切分」的外层框架，
// 只有切点判定不同，便于与 FastCDC 做同口径对比。
//

#include "Baselines.h"
#include "GearHash/GearHash.h"

#include <algorithm>
#include <cstdint>

auto fixedSizeCuts(std::string_view data,
                   const std::size_t chunkSize,
                   std::vector<std::size_t>& cuts) -> void {
    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }
    for (std::size_t pos = chunkSize; pos < n; pos += chunkSize) {
        cuts.push_back(pos);
    }
    cuts.push_back(n);
}

auto gearCuts(std::string_view data,
              const std::size_t minSize,
              const std::size_t maxSize,
              const std::size_t expectedSize,
              std::vector<std::size_t>& cuts) -> void {
    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }
    // 原始 Gear 用连续低位掩码，滑动窗口等于掩码位宽（无零填充）。
    int base = 0;
    for (std::size_t v = expectedSize; v > 1; v >>= 1) {
        ++base;
    }
    const std::uint64_t mask = (base >= 64) ? ~0ULL : ((1ULL << base) - 1ULL);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());

    std::size_t last = 0;
    while (last < n) {
        if (n - last <= minSize) {
            cuts.push_back(n);
            break;
        }
        const std::size_t searchEnd = std::min(last + maxSize, n);
        std::uint64_t hash = 0;
        std::size_t cut = searchEnd;
        for (std::size_t i = last + minSize; i < searchEnd; ++i) {
            hash = gearRoll(hash, bytes[i]);
            if ((hash & mask) == 0) {
                cut = i;
                break;
            }
        }
        cuts.push_back(cut);
        last = cut;
    }
}

auto rabinCuts(std::string_view data,
               const std::size_t minSize,
               const std::size_t maxSize,
               std::vector<std::size_t>& cuts) -> void {
    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }
    constexpr std::size_t kWindow = 48;          // LBFS 滑动窗口字节数。
    constexpr std::uint64_t kPoly = 0x3DA3358B4DC173ULL; // 固定的奇多项式常数。
    constexpr std::uint64_t kDivisorMask = 0x1FFFu;      // D = 0x2000 -> 低 13 位。
    constexpr std::uint64_t kThreshold = 0x78u;          // r = 0x78。

    // p^window mod 2^64，用于滚动时移除最旧字节。
    std::uint64_t pPow = 1;
    for (std::size_t i = 0; i < kWindow; ++i) {
        pPow *= kPoly;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t last = 0;
    while (last < n) {
        if (n - last <= minSize) {
            cuts.push_back(n);
            break;
        }
        const std::size_t searchEnd = std::min(last + maxSize, n);
        std::size_t cut = searchEnd;

        // 在 [last, searchEnd) 上维护窗口 [i-kWindow+1, i] 的多项式指纹。
        std::uint64_t fp = 0;
        std::size_t i = last;
        const std::size_t initEnd = std::min(last + kWindow, searchEnd);
        for (; i < initEnd; ++i) {
            fp = fp * kPoly + bytes[i];
        }
        for (; i < searchEnd; ++i) {
            const std::uint8_t oldByte = bytes[i - kWindow];
            fp = (fp * kPoly + bytes[i]) - static_cast<std::uint64_t>(oldByte) * pPow;
            if (i - last >= minSize && (fp & kDivisorMask) == kThreshold) {
                cut = i;
                break;
            }
        }
        cuts.push_back(cut);
        last = cut;
    }
}
