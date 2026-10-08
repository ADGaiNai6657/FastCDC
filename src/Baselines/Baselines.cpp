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
               const std::size_t expectedSize,
               std::vector<std::size_t>& cuts) -> void {
    if (expectedSize < 2) {
        return; // 防御：过小的除数无意义（掩码为 0 会恒命中）。
    }
    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }
    constexpr std::size_t kWindow = 48;          // LBFS 滑动窗口字节数。
    constexpr std::uint64_t kPoly = 0x3DA3358B4DC173ULL; // 固定的奇多项式常数。
    // 论文 §5.1：Rabin 的 max/min 随期望块长缩放，故除数 D 亦然（§2 Eq.1：D 即平均块长）。
    const std::uint64_t kDivisorMask = expectedSize - 1; // D = expectedSize。
    // r = 0x78（LBFS 口径），需严格小于 D；D 过小时退化为 0 以避免永不命中。
    const std::uint64_t kThreshold = std::min<std::uint64_t>(0x78u, expectedSize - 1);

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
