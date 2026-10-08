// FastCDC 实验驱动：对单个文件或数据集目录做 CDC 分块，并把每个块发射到
// 全局 ChunkStore 做去重（SHA-1 指纹，一个 chunk 只存一次），最后汇总论文关心的指标：
//   论文去重率 dedupRatio = 重复字节 / 总字节 = 1 - uniqueBytes / totalBytes
//   DER（压缩比）        = totalBytes / uniqueBytes
//   平均块长             = totalBytes / totalChunks
// 同时分别统计「纯分块」与「全流程（分块+去重）」的吞吐量。

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "Baselines/Baselines.h"
#include "BreakingApart/BreakingApart.h"
#include "ChunkStore/ChunkStore.h"

namespace fs = std::filesystem;

// 分块算法选择。
enum class Algo { FastCDC, Gear, Rabin, FSC };

struct Options {
    fs::path target;
    bool targetSet = false;
    bool recursive = false;
    Algo algo = Algo::FastCDC;
    int ncLevel = 0;              // 0=非 NC（默认）；--nc 等价 2。
    bool ncSet = false;
    std::size_t expectedSize = 8192;
    std::size_t minSize = 0;      // 0 表示未设置，回退到 expected/4。
    bool minSizeSet = false;
    std::size_t fscSize = 10 * 1024;
    int runs = 1;
    std::string csvPath;
    bool verbose = true;
};

// 全局块长极值。
std::size_t gMinChunk = std::numeric_limits<std::size_t>::max();
std::size_t gMaxChunk = 0;

auto readFile(const fs::path& path, std::string& buffer) -> bool {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "open failed: " << path.string() << '\n';
        return false;
    }
    buffer.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

// 根据配置调用对应分块算法，把绝对切点写入 cuts。
auto findCuts(std::string_view data,
              const Options& opts,
              const BreakingApartConfig& cfg,
              std::vector<std::size_t>& cuts) -> void {
    cuts.clear();
    switch (opts.algo) {
        case Algo::FastCDC: {
            std::vector<std::vector<std::size_t>> positions;
            BoundariesFinder(data, positions, cfg);
            cuts = positions.back();
            break;
        }
        case Algo::Gear:
            gearCuts(data, cfg.MinSize, cfg.MaxSize, opts.expectedSize, cuts);
            break;
        case Algo::Rabin:
            // Rabin 按论文用 1/4 与 8x 期望块长；Min=0 时用 expected/4。
            rabinCuts(data,
                      opts.minSizeSet ? opts.minSize : opts.expectedSize / 4,
                      opts.expectedSize * 8,
                      cuts);
            break;
        case Algo::FSC:
            fixedSizeCuts(data, opts.fscSize, cuts);
            break;
    }
}

// 对一段缓冲分块并发射；返回纯分块耗时（不含去重）。
auto processBuffer(const std::string& data,
                   const Options& opts,
                   const BreakingApartConfig& cfg,
                   double& chunkSeconds) -> void {
    std::vector<std::size_t> cuts;

    const auto t0 = std::chrono::steady_clock::now();
    findCuts(data, opts, cfg, cuts);
    const auto t1 = std::chrono::steady_clock::now();
    chunkSeconds += std::chrono::duration<double>(t1 - t0).count();

    beginStream();
    std::size_t prev = 0;
    for (const std::size_t cut : cuts) {
        if (cut <= prev) {
            continue;
        }
        const std::size_t len = cut - prev;
        gMinChunk = std::min(gMinChunk, len);
        gMaxChunk = std::max(gMaxChunk, len);
        emitChunk(data, prev, cut);
        prev = cut;
    }
}

auto processFile(const fs::path& path,
                 const Options& opts,
                 const BreakingApartConfig& cfg,
                 double& chunkSeconds) -> bool {
    std::string buffer;
    if (!readFile(path, buffer)) {
        return false;
    }
    processBuffer(buffer, opts, cfg, chunkSeconds);
    return true;
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
    std::cerr << "usage: " << argv0 << " [<file|dir>] [options]\n"
              << "  无路径参数时默认使用 Dataset/（递归）。\n"
              << "  --algo fastcdc|gear|rabin|fsc  分块算法（默认 fastcdc）\n"
              << "  --expected-size BYTES          期望块长（默认 8192）\n"
              << "  --min-size BYTES               最小块长（默认 expected/4；0 表示不跳过）\n"
              << "  --nc | --nc-level N            归一化分块等级 1/2/3（--nc 等价 2）\n"
              << "  --no-nc                        关闭归一化分块\n"
              << "  --fsc-size BYTES               FSC 固定块长（默认 10240）\n"
              << "  --recursive | -r               递归遍历目录\n"
              << "  --runs N                       重复运行次数取平均（默认 1）\n"
              << "  --csv PATH                     追加一行结果到 CSV\n";
}

auto parseArgs(int argc, char** argv, Options& opts) -> bool {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                std::exit(1);
            }
            return argv[++i];
        };
        if (arg == "--algo") {
            const std::string v = needValue("--algo");
            if (v == "fastcdc") opts.algo = Algo::FastCDC;
            else if (v == "gear") opts.algo = Algo::Gear;
            else if (v == "rabin") opts.algo = Algo::Rabin;
            else if (v == "fsc" || v == "xc") opts.algo = Algo::FSC;
            else { std::cerr << "unknown algo: " << v << '\n'; return false; }
        } else if (arg == "--expected-size") {
            opts.expectedSize = std::stoull(needValue("--expected-size"));
        } else if (arg == "--min-size") {
            opts.minSize = std::stoull(needValue("--min-size"));
            opts.minSizeSet = true;
        } else if (arg == "--nc") {
            opts.ncLevel = 2;
            opts.ncSet = true;
        } else if (arg == "--nc-level") {
            opts.ncLevel = std::stoi(needValue("--nc-level"));
            opts.ncSet = true;
        } else if (arg == "--no-nc") {
            opts.ncLevel = 0;
            opts.ncSet = true;
        } else if (arg == "--fsc-size") {
            opts.fscSize = std::stoull(needValue("--fsc-size"));
        } else if (arg == "--recursive" || arg == "-r") {
            opts.recursive = true;
        } else if (arg == "--runs") {
            opts.runs = std::max(1, std::stoi(needValue("--runs")));
        } else if (arg == "--csv") {
            opts.csvPath = needValue("--csv");
        } else if (arg == "--quiet") {
            opts.verbose = false;
        } else if (arg.rfind("--", 0) != 0 && arg.rfind("-", 0) != 0) {
            opts.target = arg;
            opts.targetSet = true;
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return false;
        }
    }
    return true;
}

auto algoName(const Algo a) -> const char* {
    switch (a) {
        case Algo::FastCDC: return "FastCDC";
        case Algo::Gear: return "GearCDC";
        case Algo::Rabin: return "RabinCDC";
        case Algo::FSC: return "FSC";
    }
    return "?";
}

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 1;
    }

    if (!opts.targetSet) {
        opts.target = datasetRoot();
        opts.recursive = true;
    }
    if (opts.minSize == 0 && !opts.minSizeSet) {
        opts.minSize = opts.expectedSize / 4;
    }

    const BreakingApartConfig cfg = makeFastCDCConfig(opts.expectedSize, opts.minSize, opts.ncLevel);

    std::error_code ec;
    if (!fs::exists(opts.target, ec)) {
        std::cerr << "path not found: " << opts.target.string() << '\n';
        return 1;
    }

    std::vector<fs::path> files;
    if (fs::is_regular_file(opts.target, ec)) {
        files.push_back(opts.target);
    } else if (fs::is_directory(opts.target, ec)) {
        collectFiles(opts.target, opts.recursive, files);
    } else {
        std::cerr << "not a file or directory: " << opts.target.string() << '\n';
        return 1;
    }
    std::sort(files.begin(), files.end());

    if (opts.verbose) {
        std::cout << "target=" << opts.target.string()
                  << (opts.recursive ? "（递归）" : "")
                  << "  algo=" << algoName(opts.algo)
                  << "  expected=" << opts.expectedSize
                  << " min=" << opts.minSize
                  << " ncLevel=" << opts.ncLevel
                  << "  files=" << files.size() << '\n';
    }

    const bool perFile = opts.verbose && files.size() <= 32 && opts.runs == 1;

    double totalChunkSeconds = 0.0;
    double totalFullSeconds = 0.0;

    for (int run = 0; run < opts.runs; ++run) {
        resetStore();
        gMinChunk = std::numeric_limits<std::size_t>::max();
        gMaxChunk = 0;

        const auto runStart = std::chrono::steady_clock::now();
        double runChunkSeconds = 0.0;
        std::size_t processed = 0;
        for (const auto& path : files) {
            if (processFile(path, opts, cfg, runChunkSeconds)) {
                ++processed;
            }
            if (perFile) {
                std::cout << "  " << path.filename().string()
                          << " chunks=" << gTotalChunks
                          << " unique=" << gChunkPool.size() << '\n';
            }
        }
        const auto runEnd = std::chrono::steady_clock::now();
        totalChunkSeconds += runChunkSeconds;
        totalFullSeconds += std::chrono::duration<double>(runEnd - runStart).count();
        (void)processed;
    }

    const double avgChunkSeconds = totalChunkSeconds / opts.runs;
    const double avgFullSeconds = totalFullSeconds / opts.runs;

    const double dedupRatio = gTotalBytes
                                  ? 1.0 - static_cast<double>(gUniqueBytes) / static_cast<double>(gTotalBytes)
                                  : 0.0;
    const double der = gUniqueBytes
                           ? static_cast<double>(gTotalBytes) / static_cast<double>(gUniqueBytes)
                           : 0.0;
    const double avgChunk = gTotalChunks
                                ? static_cast<double>(gTotalBytes) / static_cast<double>(gTotalChunks)
                                : 0.0;
    const double chunkMBps = avgChunkSeconds > 0.0
                                 ? static_cast<double>(gTotalBytes) / avgChunkSeconds / (1024.0 * 1024.0)
                                 : 0.0;
    const double fullMBps = avgFullSeconds > 0.0
                                ? static_cast<double>(gTotalBytes) / avgFullSeconds / (1024.0 * 1024.0)
                                : 0.0;

    std::cout << "algo=" << algoName(opts.algo)
              << " expected=" << opts.expectedSize
              << " min=" << opts.minSize
              << " ncLevel=" << opts.ncLevel
              << " totalChunks=" << gTotalChunks
              << " uniqueChunks=" << gChunkPool.size()
              << " duplicateChunks=" << gDupChunks
              << " totalBytes=" << gTotalBytes
              << " uniqueBytes=" << gUniqueBytes << '\n'
              << "dedupRatio=" << std::fixed << std::setprecision(4) << dedupRatio
              << " DER=" << der
              << " avgChunk=" << std::setprecision(2) << avgChunk
              << " minChunk=" << gMinChunk
              << " maxChunk=" << gMaxChunk
              << " chunkMBps=" << std::setprecision(2) << chunkMBps
              << " fullMBps=" << std::setprecision(2) << fullMBps
              << " runs=" << opts.runs << '\n';

    if (!opts.csvPath.empty()) {
        std::error_code csvEc;
        const bool needHeader = !fs::exists(opts.csvPath, csvEc) || fs::file_size(opts.csvPath, csvEc) == 0;
        std::ofstream ofs(opts.csvPath, std::ios::app);
        if (!ofs) {
            std::cerr << "cannot write csv: " << opts.csvPath << '\n';
            return 1;
        }
        if (needHeader) {
            ofs << "target,algo,expectedSize,minSize,ncLevel,totalChunks,uniqueChunks,"
                   "duplicateChunks,totalBytes,uniqueBytes,dedupRatio,DER,avgChunk,minChunk,"
                   "maxChunk,chunkMBps,fullMBps,runs\n";
        }
        ofs << opts.target.string() << ',' << algoName(opts.algo) << ',' << opts.expectedSize
            << ',' << opts.minSize << ',' << opts.ncLevel << ',' << gTotalChunks
            << ',' << gChunkPool.size() << ',' << gDupChunks << ',' << gTotalBytes
            << ',' << gUniqueBytes << ',' << std::fixed << std::setprecision(6) << dedupRatio
            << ',' << der << ',' << std::setprecision(2) << avgChunk << ',' << gMinChunk
            << ',' << gMaxChunk << ',' << chunkMBps << ',' << fullMBps << ',' << opts.runs << '\n';
    }
    return 0;
}
