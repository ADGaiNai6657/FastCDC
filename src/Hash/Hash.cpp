//
// Hash.cpp
// OpenSSL EVP 实现。复用 thread_local 的 EVP_MD_CTX，避免在热循环里每块
// 重复 new/free；使用 EVP API 而非已弃用的 SHA1_* 低层函数。

#include "Hash/Hash.h"

#include <cstring>

#include <openssl/evp.h>

namespace {

    // 单次 SHA-1 摘要计算。ctx 为 thread_local：每线程仅分配一次并常驻复用。
    auto sha1Into(std::string_view data, unsigned char* out) -> void {
        thread_local EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        unsigned int length = 0;
        EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
        EVP_DigestUpdate(ctx, data.data(), data.size());
        EVP_DigestFinal_ex(ctx, out, &length);
    }

} // namespace

// 内容哈希入口：返回 20 字节摘要，作为唯一块的内容指纹。
auto sha1(std::string_view data) -> Sha1Digest {
    Sha1Digest digest{};
    sha1Into(data, digest.data());
    return digest;
}

// 把 20 字节摘要压成桶号：直接 memcpy 前 8 字节（机器字节序），比逐字节混合快。
auto Sha1DigestHash::operator()(const Sha1Digest& digest) const noexcept -> std::size_t {
    std::uint64_t value = 0;
    std::memcpy(&value, digest.data(), sizeof(value));
    return static_cast<std::size_t>(value);
}
