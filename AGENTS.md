# AGENTS.md

FastCDC：基于 `paper/atc16-paper-xia.pdf` 的 C++20 内容定义分块（CDC）实现。
单一 CMake CLI 目标；无测试、lint、CI。

## 构建与运行

- 需要 CMake >= 4.1（`cmake_minimum_required(VERSION 4.1)`）。
- 构建：`cmake -B cmake-build-debug && cmake --build cmake-build-debug`
- 运行：`./cmake-build-debug/FastCDC <file> [--nc]`（`--nc` 启用归一化分块）
- 不用 CMake 的快速编译（头文件经 `src/` 解析）：
  `g++ -std=c++20 -Isrc src/main.cpp src/GearHash/GearHash.cpp src/BreakingApart/BreakingApart.cpp -o fastcdc`

## 验证

没有测试套件。验证方式：构建后对数据集文件运行并检查输出的块长统计：
`./cmake-build-debug/FastCDC Dataset/DataSet_1/emacs-22.1.tar.gz --nc`
默认参数（target 8096，min 4048，max 64768）下，非 NC 平均约 12k，
NC 平均约 9.6k，最大块长不会超过 MaxSize（64768）。

## 代码布局

- `src/GearHash/` —— `GEAR_TABLE[256]`、头文件内联的 `gearRoll()` 与
  `updateGearHash()`；滚动哈希为 `hash = (hash << 1) + GEAR_TABLE[byte]`。
- `src/BreakingApart/` —— `BoundariesFinder()` 向 `vector<vector<size_t>>`
  填入切点偏移（绝对字节位置；最后一项 == 文件大小）。
- `src/main.cpp` —— 读取文件、调用 `BoundariesFinder`、打印 min/avg/max 块长。

## 注意事项

- `BoundariesFinder` 忽略配置参数，使用 `BreakingApart.cpp` 中的文件级
  `breakingApartConfig`；调整分块参数需改这里。
- 掩码：NC 在 `NormalSize` 前用 `MaskS`、之后用 `MaskL`；非 NC 用 `MaskM`；
  `MaxSize` 强制切分。改动这些会直接改变平均块长。
- 项目头文件按 `"GearHash/GearHash.h"` / `"BreakingApart/BreakingApart.h"` 引用。

## 仓库约定

- `Dataset/`（约 800MB emacs 语料）未跟踪 —— 切勿提交。
- 提交信息与代码注释使用中文。
- 函数使用尾置返回类型（`auto f(...) -> T`）。
