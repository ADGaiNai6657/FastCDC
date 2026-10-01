//
// Created by ADGaiNai6657 on 2026/9/14.
//
// ChunkStore.h
// 去重存储的全局状态、内容索引与统计量。
// 数据流：BoundariesFinder 产生边界 -> emitChunk 切片发射 -> chunkStore 去重
//         -> 写入 gChunkPool/gChunkIndex。
// gChunkPool/gChunkIndex 跨文件保留，是全局去重（一个 chunk 只存一次）的基础。
//
// 内容指纹复用现有的 GearHash：对整块字节滚动 hash=(hash<<1)+GEAR_TABLE[b]。
// 它是非密码学 64 位指纹，故索引用 multimap 并按内容逐字节确认，保证去重正确。

#ifndef FASTCDC_CHUNKSTORE_H
#define FASTCDC_CHUNKSTORE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 内容哈希：整块 GearHash 的 64 位指纹。
using ChunkHash = std::uint64_t;

// 唯一块：独占一份内容，代表全局去重后真正需要存储的数据。
struct Chunk {
    ChunkHash hash;      // 内容指纹。
    std::string data;    // 内容副本（源缓冲释放后仍可用）。
    std::size_t length;  // data.size()，缓存以避免反复调用。
};

// 一次“块出现”事件：指向某个唯一块，并记录它在源流中的起始偏移。
struct ChunkRef {
    Chunk* chunk;
    std::size_t offset;
};

// 唯一块池：deque 保证 push_back 后既有的 Chunk* 不失效。
inline std::deque<Chunk> gChunkPool;
// 内容索引：一个指纹可对应多个 Chunk*（碰撞候选），命中后再逐字节确认。
inline std::unordered_multimap<ChunkHash, Chunk*> gChunkIndex;
inline std::size_t gTotalChunks = 0;  // 发射的块总数（含重复）。
inline std::size_t gDupChunks = 0;    // 命中去重的块数。
inline std::size_t gTotalBytes = 0;   // 发射的总字节数（等于输入字节数）。
inline std::size_t gUniqueBytes = 0;  // 唯一块占用的总字节数。
// 每个输入流（文件）一组出现记录，元素为轻量 ChunkRef（不持有内容）。
inline std::vector<std::vector<ChunkRef>> vChunks;

// 开始一条新的输入流（文件）：为其准备一组出现记录。
auto beginStream() -> void;

// 内容去重入口：命中则返回既有唯一块，未命中则新建；由 emitChunk 调用。
// 恒等式：调用 N 次后 gTotalChunks == gChunkPool.size() + gDupChunks。
auto chunkStore(Chunk chunk) -> Chunk*;

// 存在性查询：只看指纹、不校验内容，可能因碰撞产生假阳性。
auto isExist(const ChunkHash& hash) -> bool;

// 精确存在性查询：命中返回唯一块指针，否则 nullptr；只读，不改索引与统计。
auto lookup(std::string_view content) -> Chunk*;

// 把 data 的 [begin, end) 切片交给 chunkStore 去重，并记录一条出现记录。
auto emitChunk(std::string_view data, std::size_t begin, std::size_t end) -> void;

#endif //FASTCDC_CHUNKSTORE_H
