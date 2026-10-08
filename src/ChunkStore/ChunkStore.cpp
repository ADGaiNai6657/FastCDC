//
// Created by ADGaiNai6657 on 2026/9/14.
//
// ChunkStore 的实现：内容去重与索引。
// 只有唯一块写入 gChunkPool；重复块只返回既有指针，因此块内容在池中独占一份。

#include <utility>

#include "ChunkStore/ChunkStore.h"

namespace {

    // 对整块内容求 SHA-1 160 位指纹，作为去重索引 key（与论文一致）。
    auto contentHash(std::string_view content) -> ChunkHash {
        return sha1(content);
    }

} // namespace

// 开始一条新流，为出现记录准备一组空向量。
auto beginStream() -> void {
    vChunks.emplace_back();
}

// 清空去重存储与全部统计量，供多轮实验重置。
auto resetStore() -> void {
    gChunkPool.clear();
    gChunkIndex.clear();
    gTotalChunks = 0;
    gDupChunks = 0;
    gTotalBytes = 0;
    gUniqueBytes = 0;
    vChunks.clear();
}

// 内容去重的唯一入口：对块做「查重 -> 记录 -> 返回唯一块指针」。
auto chunkStore(Chunk chunk) -> Chunk* {
    // 第 1 步：内容指纹。同一段字节在任何文件、任何位置都应得到相同指纹。
    const ChunkHash hash = contentHash(chunk.data);

    // 第 2 步：统计“发射”维度（含重复块）。
    ++gTotalChunks;
    gTotalBytes += chunk.length;

    // 第 3 步：取出全部同指纹候选（碰撞的块会排在一起）。
    const auto range = gChunkIndex.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        Chunk* candidate = it->second;
        // 第 4 步：逐字节确认。指纹相同不足以判定重复，必须内容完全一致。
        if (candidate->length == chunk.length && candidate->data == chunk.data) {
            ++gDupChunks;      // 逐字节确认后才算重复。
            return candidate;  // 复用唯一块，不再复制内容。
        }
        // 长度/内容不符 -> 只是指纹碰撞，继续看下一个候选。
    }

    // 第 5 步：未命中，新建唯一块。move 进 deque 后取稳定指针再登记。
    chunk.hash = hash;
    gChunkPool.push_back(std::move(chunk));
    Chunk* stored = &gChunkPool.back();
    gChunkIndex.emplace(hash, stored);
    gUniqueBytes += stored->length;
    return stored;
}

// 只看指纹的存在性查询（可能假阳性），供统计/快速判断使用。
auto isExist(const ChunkHash& hash) -> bool {
    return gChunkIndex.contains(hash);
}

// 精确存在性查询：与 chunkStore 的查重段相同，但只读、不计数、不插入。
auto lookup(std::string_view content) -> Chunk* {
    const ChunkHash hash = contentHash(content);
    const auto range = gChunkIndex.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        Chunk* candidate = it->second;
        if (candidate->length == content.size() && std::string_view(candidate->data) == content) {
            return candidate;
        }
    }
    return nullptr;
}

// 把 [begin, end) 切片构造成 Chunk 后交给 chunkStore 去重，并记录出现位置。
auto emitChunk(std::string_view data, std::size_t begin, std::size_t end) -> void {
    if (end <= begin) {
        return; // 跳过 0 长尾块。
    }
    Chunk chunk{ChunkHash{}, std::string(data.substr(begin, end - begin)), end - begin};
    Chunk* stored = chunkStore(std::move(chunk));
    vChunks.back().push_back(ChunkRef{stored, begin});
}
