# AGENTS.md

FastCDC：基于 `paper/atc16-paper-xia.pdf` 的 C++20 内容定义分块（CDC）实现。
单一 CMake CLI 目标；无测试、lint、CI。

## 构建与运行

- 需要 CMake >= 4.1（`cmake_minimum_required(VERSION 4.1)`）。
- 构建：`cmake -B cmake-build-debug && cmake --build cmake-build-debug`
- 运行：`./cmake-build-debug/FastCDC [<file|dir>] [--nc] [--recursive]`
  （无路径参数时默认 `Dataset/` 且递归；`--nc` 启用归一化分块）
- 不用 CMake 的快速编译（头文件经 `src/` 解析）：
  `g++ -std=c++20 -Isrc src/main.cpp src/GearHash/GearHash.cpp src/BreakingApart/BreakingApart.cpp src/ChunkStore/ChunkStore.cpp -o fastcdc`

## 验证

没有测试套件。验证方式：构建后对数据集运行并检查块长与去重统计：
`./cmake-build-debug/FastCDC Dataset/DataSet_2 --recursive --nc`
默认参数（target 8096，min 4048，max 64768）下，非 NC 平均约 12k、
NC 平均约 9.6k，最大块长不超过 MaxSize；输出含 `dedupRatio`（DER =
totalBytes/uniqueBytes）。`Dataset/DataSet_1` 是 gzip 压缩包，字节级重复少、DER≈1，
去重实验应使用未压缩的 `DataSet_2`。

## 代码布局

- `src/GearHash/` —— `GEAR_TABLE[256]`、头文件内联的 `gearRoll()` 与
  `updateGearHash()`；滚动哈希为 `hash = (hash << 1) + GEAR_TABLE[byte]`。
- `src/BreakingApart/` —— `BoundariesFinder()` 向 `vector<vector<size_t>>`
  填入切点偏移（绝对字节位置；最后一项 == 文件大小）。
- `src/ChunkStore/` —— 全局去重存储：`gChunkPool`（`deque<Chunk>`，唯一块各存一份）
  与 `gChunkIndex`（`multimap`，指纹→`Chunk*`）；`emitChunk()` 切片发射，
  `chunkStore()` 去重。内容指纹复用 `updateGearHash`（64 位，非密码学）。
- `src/main.cpp` —— 数据集驱动：遍历文件、分块、`emitChunk` 去重，汇总
  `dedupRatio`/`avgChunk`/`minChunk`/`maxChunk`/吞吐量。

## 注意事项

- `BoundariesFinder` 忽略配置参数，使用 `BreakingApart.cpp` 中的文件级
  `breakingApartConfig`；调整分块参数需改这里。
- 掩码：NC 在 `NormalSize` 前用 `MaskS`、之后用 `MaskL`；非 NC 用 `MaskM`；
  `MaxSize` 强制切分。改动这些会直接改变平均块长。
- 去重正确性依赖「指纹分桶 + `chunkStore` 逐字节确认」，指纹碰撞不会导致误判；
  但每个唯一块会在内存中保留一份内容副本（`gChunkPool`），大数据集注意内存。
- 项目头文件按 `"GearHash/GearHash.h"` / `"BreakingApart/BreakingApart.h"` /
  `"ChunkStore/ChunkStore.h"` 引用。无第三方依赖（去重指纹即 GearHash）。

## 仓库约定

- `Dataset/`（约 800MB emacs 语料）未跟踪 —— 切勿提交。
- 提交信息与代码注释使用中文。
- 函数使用尾置返回类型（`auto f(...) -> T`）。
