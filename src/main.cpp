// FastCDC 实验驱动：对单个文件或数据集目录做 CDC 分块，并把每个块发射到
// 全局 ChunkStore 做去重（同一个 chunk 只存一次），最后汇总论文关心的指标：
// 去重率 DER = totalBytes / uniqueBytes、平均块长 = totalBytes / totalChunks。

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "BreakingApart/BreakingApart.h"
#include "ChunkStore/ChunkStore.h"

namespace fs = std::filesystem;

// 累计统计快照，用于计算「单个文件」的增量。
struct RunStats {
    std::size_t totalChunks;
    std::size_t uniqueChunks;
    std::size_t dupChunks;
    std::size_t totalBytes;
    std::size_t uniqueBytes;
};

auto snapshotStats() -> RunStats {
    return {gTotalChunks, gChunkPool.size(), gDupChunks, gTotalBytes, gUniqueBytes};
}

// 全局块长极值。
std::size_t gMinChunk = std::numeric_limits<std::size_t>::max();
std::size_t gMaxChunk = 0;

// 读取整个文件为二进制缓冲。
auto readFile(const fs::path& path, std::string& buffer) -> bool {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "open failed: " << path.string() << '\n';
        return false;
    }
    buffer.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

// 对一段缓冲分块并发射：每个切点区间 [prev, cut) 交给 emitChunk 去重。
auto processBuffer(const std::string& data, const bool isNC) -> void {
    std::vector<std::vector<size_t>> positions;
    BoundariesFinder(data, positions, isNC);

    beginStream();
    const auto& cuts = positions.back();

    std::size_t prev = 0;
    for (const std::size_t cut : cuts) {
        const std::size_t len = cut - prev;
        gMinChunk = std::min(gMinChunk, len);
        gMaxChunk = std::max(gMaxChunk, len);
        emitChunk(data, prev, cut);
        prev = cut;
    }
}

// 处理单个文件，返回是否成功。
auto processFile(const fs::path& path, const bool isNC) -> bool {
    std::string buffer;
    if (!readFile(path, buffer)) {
        return false;
    }
    processBuffer(buffer, isNC);
    return true;
}

// 目录下文件数量不多时逐文件报告增量，便于观察备份流各版本的去重效果。
auto reportFileDelta(const fs::path& path, const RunStats& before, const RunStats& after) -> void {
    const double cumDer = after.uniqueBytes
                              ? static_cast<double>(after.totalBytes) / static_cast<double>(after.uniqueBytes)
                              : 0.0;
    std::cout << "  " << path.filename().string()
              << " size=" << (after.totalBytes - before.totalBytes)
              << " chunks+=" << (after.totalChunks - before.totalChunks)
              << " unique+=" << (after.uniqueChunks - before.uniqueChunks)
              << " dup+=" << (after.dupChunks - before.dupChunks)
              << " cumDER=" << std::fixed << std::setprecision(4) << cumDer << '\n';
}

auto collectFiles(const fs::path& target, const bool recursive, std::vector<fs::path>& out) -> void {
    std::error_code ec;
    if (recursive) {
        for (fs::recursive_directory_iterator it(target, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                out.push_back(it->path());
            }
        }
    } else {
        for (fs::directory_iterator it(target, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                out.push_back(it->path());
            }
        }
    }
}

// 兼容在构建目录或仓库根目录运行：优先 ../Dataset，其次 Dataset。
auto datasetRoot() -> fs::path {
    std::error_code ec;
    if (fs::is_directory("../Dataset", ec)) {
        return "../Dataset";
    }
    if (fs::is_directory("Dataset", ec)) {
        return "Dataset";
    }
    return "Dataset";
}

auto printUsage(const char* argv0) -> void {
    std::cerr << "usage: " << argv0 << " [<file|dir>] [--nc] [--recursive]\n"
              << "  无路径参数时默认使用 Dataset/（递归）。\n"
              << "  --nc        启用归一化分块（normalized chunking）。\n"
              << "  --recursive 递归遍历目录。\n";
}

int main(int argc, char** argv) {
    fs::path target;
    bool isNC = false;
    bool recursive = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--nc") {
            isNC = true;
        } else if (arg == "--recursive" || arg == "-r") {
            recursive = true;
        } else if (arg.rfind("--", 0) != 0) {
            target = arg;
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            printUsage(argv[0]);
            return 1;
        }
    }

    if (target.empty()) {
        target = datasetRoot();
        recursive = true;
    }

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        std::cerr << "path not found: " << target.string() << '\n';
        return 1;
    }

    std::vector<fs::path> files;
    if (fs::is_regular_file(target, ec)) {
        files.push_back(target);
    } else if (fs::is_directory(target, ec)) {
        collectFiles(target, recursive, files);
    } else {
        std::cerr << "not a file or directory: " << target.string() << '\n';
        return 1;
    }

    // 稳定顺序：保证备份按版本先后处理（backup stream 语义）。
    std::sort(files.begin(), files.end());

    std::cout << "target=" << target.string()
              << (recursive ? "（递归）" : "")
              << "  algo=" << (isNC ? "FastCDC-NC" : "FastCDC")
              << "  files=" << files.size() << '\n';

    const bool perFile = files.size() <= 32;
    const auto start = std::chrono::steady_clock::now();

    std::size_t processed = 0;
    for (const auto& path : files) {
        if (!perFile) {
            if (processFile(path, isNC)) {
                ++processed;
            }
            continue;
        }
        const RunStats before = snapshotStats();
        if (processFile(path, isNC)) {
            ++processed;
            reportFileDelta(path, before, snapshotStats());
        }
    }

    const auto end = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(end - start).count();

    const double der = gUniqueBytes
                           ? static_cast<double>(gTotalBytes) / static_cast<double>(gUniqueBytes)
                           : 0.0;
    const double avgChunk = gTotalChunks
                                ? static_cast<double>(gTotalBytes) / static_cast<double>(gTotalChunks)
                                : 0.0;
    const double mbps = seconds > 0.0
                            ? static_cast<double>(gTotalBytes) / seconds / (1024.0 * 1024.0)
                            : 0.0;

    // 恒等式：uniqueChunks + duplicateChunks == totalChunks。
    std::cout << "files=" << processed
              << " totalChunks=" << gTotalChunks
              << " uniqueChunks=" << gChunkPool.size()
              << " duplicateChunks=" << gDupChunks
              << " totalBytes=" << gTotalBytes
              << " uniqueBytes=" << gUniqueBytes << '\n'
              << "dedupRatio=" << std::fixed << std::setprecision(4) << der
              << " avgChunk=" << std::setprecision(2) << avgChunk
              << " minChunk=" << gMinChunk
              << " maxChunk=" << gMaxChunk
              << " elapsedSec=" << std::setprecision(3) << seconds
              << " throughputMBps=" << std::setprecision(2) << mbps << '\n';
    return 0;
}
