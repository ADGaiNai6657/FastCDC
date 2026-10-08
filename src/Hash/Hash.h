//
// Hash.h
// 基于 OpenSSL EVP 的 SHA-1 封装（移植自 Bimodal 项目 src/Hash.*）。
// 用途：作为去重索引的完整 160 位内容摘要，替代原先 64 位 GearHash 指纹，
// 与 FastCDC 论文（USENIX ATC'16）使用的 SHA-1 指纹口径一致。
// 分块边界仍使用 Gear 滚动哈希（见 GearHash/），两者互不影响。

#ifndef FASTCDC_HASH_H
#define FASTCDC_HASH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// SHA-1 摘要：固定 160 位 = 20 字节，作为去重索引的 key。
using Sha1Digest = std::array<std::uint8_t, 20>;

// 内容哈希入口：对一段字节计算完整 SHA-1 摘要（160 位）。
auto sha1(std::string_view data) -> Sha1Digest;

// Sha1Digest 的无序容器哈希函数：把 20 字节摘要压成一个 size_t 桶号。
// 摘要本身已均匀分布，桶号只需「同 key 同值」，不要求跨平台一致。
struct Sha1DigestHash {
    auto operator()(const Sha1Digest& digest) const noexcept -> std::size_t;
};

#endif //FASTCDC_HASH_H
