//
// BreakingApart.cpp
// FastCDC 边界查找，实现论文 Algorithm 1 的三项技术：
//   - 优化哈希判定 (fp & Mask) == 0（零填充掩码放大滑动窗口）
//   - 次最小块切点跳过（从 last + MinSize 起滚动）
//   - 归一化分块 NC（NormalSize 前后切换 MaskS / MaskL）
//

#include "BreakingApart.h"
#include "GearHash/GearHash.h"

#include <algorithm>
#include <bit>

namespace {

    // 论文 Algorithm 1 公开的三个掩码常量（8KB 期望块长）。
    constexpr std::uint64_t MASK_A_13 = 0x0000d90303530000ULL; // 13 个 1
    constexpr std::uint64_t MASK_S_15 = 0x0003590703530000ULL; // 15 个 1
    constexpr std::uint64_t MASK_L_11 = 0x0000d90003530000ULL; // 11 个 1

    // 生成具有 bits 个 1 的掩码：在 [16, 49] 上等距分布，低位留零以保证
    // 至少 16 字节的滑动窗口。11/13/15 直接使用论文公开常量。
    auto buildMask(const int bits) -> std::uint64_t {
        if (bits <= 0) {
            return 0;
        }
        switch (bits) {
            case 11: return MASK_L_11;
            case 13: return MASK_A_13;
            case 15: return MASK_S_15;
            default: break;
        }
        constexpr int kLo = 16;
        constexpr int kHi = 49;
        std::uint64_t mask = 0;
        for (int i = 0; i < bits; ++i) {
            const int pos = (bits == 1)
                                ? kLo
                                : static_cast<int>(kLo + (kHi - kLo) * static_cast<long long>(i) / (bits - 1));
            mask |= (1ULL << pos);
        }
        return mask;
    }

    // 前段（< NormalSize）与后段（>= NormalSize）使用的掩码。
    auto maskFor(const BreakingApartConfig& cfg, const std::size_t chunkOffset, const bool isNC) -> std::uint64_t {
        if (!isNC) {
            return cfg.MaskA;
        }
        return (chunkOffset < cfg.NormalSize) ? cfg.MaskS : cfg.MaskL;
    }

} // namespace

auto makeFastCDCConfig(const std::size_t expectedSize,
                       const std::size_t minSize,
                       const int ncLevel) -> BreakingApartConfig {
    // base = 期望块长的二进制位宽，如 8192 -> 13。
    int base = 0;
    for (std::size_t v = expectedSize; v > 1; v >>= 1) {
        ++base;
    }
    const int level = std::clamp(ncLevel, 0, 3);
    const std::uint64_t maskA = buildMask(base);
    const std::uint64_t maskS = (level > 0) ? buildMask(base + level) : maskA;
    const std::uint64_t maskL = (level > 0) ? buildMask(std::max(1, base - level)) : maskA;
    // NC 切换点 NormalSize。论文 §5.4 将 NC 的归一化块长取为「4KB + 最小块长」，
    // 而 Algorithm 1 的 8KB 示例（Min2KB）对应 minSize + expected/2 = 6KB。
    // 取 max 同时满足两者，并保证 NormalSize > MinSize（否则第一段循环从
    // last+MinSize 起，chunkOffset 恒 >= NormalSize，MaskS 永不参与、NC 退化）：
    //   min=expected/4 -> 6KB < 8KB，取 8KB（与 Algorithm 1 一致）；
    //   Min4KB -> 8KB、Min8KB -> 12KB（与 §5.4 一致）。
    // NC 关闭时仍取 expectedSize，不影响非 NC 路径。
    const std::size_t normalSize = (level > 0)
                                       ? std::max(expectedSize, minSize + expectedSize / 2)
                                       : expectedSize;
    return {minSize, normalSize, expectedSize * 8, maskA, maskS, maskL, level};
}

auto BoundariesFinder(std::string_view data,
                      std::vector<std::vector<std::size_t>>& vPosition,
                      const BreakingApartConfig& config) -> void {
    vPosition.emplace_back();
    auto& cuts = vPosition.back();

    const std::size_t n = data.size();
    if (n == 0) {
        return;
    }

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    const bool isNC = config.NcLevel > 0;

    std::size_t last = 0; // 当前块起始下标（上一次切点）。
    while (last < n) {
        // 剩余数据不足 MinSize，直接作为最后一块切到末尾。
        if (n - last <= config.MinSize) {
            cuts.push_back(n);
            break;
        }

        const std::size_t searchEnd = std::min(last + config.MaxSize, n);

        std::uint64_t hash = 0;
        std::size_t cut = searchEnd; // 始终未命中掩码则在 MaxSize 处强制切分。

        // 跳过 MinSize 区间；此后每滚入一字节判断一次掩码。
        for (std::size_t i = last + config.MinSize; i < searchEnd; ++i) {
            hash = gearRoll(hash, bytes[i]);
            if ((hash & maskFor(config, i - last, isNC)) == 0) {
                cut = i;
                break;
            }
        }

        cuts.push_back(cut);
        last = cut;
    }
}
