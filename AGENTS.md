# AGENTS.md

FastCDC：基于 `paper/atc16-paper-xia.pdf` 的 C++20 内容定义分块（CDC）实现，
含论文第 5 章实验的复现能力（基线算法、参数化、NC 等级）。单一 CMake CLI 目标；
依赖 OpenSSL（SHA-1 去重指纹）；无测试、lint、CI。

## 构建与运行

- 需要 CMake >= 4.1（`cmake_minimum_required(VERSION 4.1)`）与 OpenSSL（keg-only 由 CMake 自动探测）。
- 构建：`cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release`
  （默认 Release `-O3`，复现速度实验必须用 Release）。
- 运行：`./cmake-build-release/FastCDC [<file|dir>] [options]`
  - `--algo fastcdc|gear|rabin|fsc`（默认 fastcdc）
  - `--expected-size N`（默认 8192）、`--min-size N`（默认 expected/4；0 表示不跳过）
  - `--nc`（= level 2）、`--nc-level 1|2|3`、`--no-nc`
  - `--fsc-size N`（默认 10240）、`--runs N`、`--csv PATH`、`--recursive|-r`、`--quiet`
  - 无路径参数时默认 `Dataset/` 且递归。
- 一键复现实验：`tools/run_experiments.sh`（结果写入 `results/*.csv`）。

## 验证

复现报告见 `paper/REPRODUCTION.md`，原始数据见 `results/*.csv`。
快速自检：

```bash
./cmake-build-release/FastCDC Dataset/DataSet_2 --recursive --no-nc        # FC-Min2K
./cmake-build-release/FastCDC Dataset/DataSet_2 --recursive --nc           # FC-NC-Min2K
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo rabin               # RC 基线
```

要点：非 NC 平均块长 ≈ `MinSize + 期望块长`；NC 会向 `NormalSize` 收窄；
`dedupRatio` 为论文定义（重复字节/总字节 = `1 - uniqueBytes/totalBytes`），
`DER = totalBytes/uniqueBytes`。数据集见下节。

## 数据集

- `Dataset/DataSet_1` —— 5 个 emacs tar.gz（压缩，去重失真，仅作原始素材）。
- `Dataset/DataSet_2` —— 5 个 emacs 源码版本（LNX 类，递归）。
- `Dataset/DataSet_3` —— 由 DataSet_1 解压得到的未压缩 tar（TAR 类）。
- `Dataset/DataSet_4` —— `tools/make_synthetic_backups.py` 生成的合成备份流（SYN 类）。
- `Dataset/` 全部未跟踪（约 1.5GB）。WEB/VMA/VMB/RDB 缺失。

### 数据集生成（DataSet_3 / DataSet_4）

> 前置条件：`Dataset/DataSet_1`（5 个 `emacs-*.tar.gz`）已就位。全部命令在仓库根执行；
> 结果确定、可复现，同名文件会被覆盖。生成顺序：先 DataSet_3，再 DataSet_4（后者以前者为基）。

- **DataSet_3（TAR 类）**：把 DataSet_1 的 gzip 流解压为未压缩 tar（去掉 `.gz` 后缀），
  无需额外脚本：

  ```bash
  mkdir -p Dataset/DataSet_3
  for f in Dataset/DataSet_1/*.tar.gz; do
      gunzip -c "$f" > "Dataset/DataSet_3/$(basename "${f%.gz}")"
  done
  ```

  产物：`Dataset/DataSet_3/emacs-{21.4a,22.1,22.2,22.3,23.1}.tar`（共约 650MB）。
  校验：各文件大小应等于 `gzip -l Dataset/DataSet_1/*.tar.gz` 输出的 uncompressed 列。

- **DataSet_4（SYN 类）**：`tools/make_synthetic_backups.py` 生成「集中且反复变更」的合成备份。
  默认参数即复现当前数据集：

  ```bash
  python3 tools/make_synthetic_backups.py \
      --base Dataset/DataSet_3/emacs-22.1.tar \
      --out  Dataset/DataSet_4 \
      --size 33554432 --backups 8 --rois 64 \
      --roi-size 8192 --edit-inner 4096 --seed 20260917
  ```

  产物：`Dataset/DataSet_4/backup_00.bin … backup_07.bin`（每个 32MiB，共 256MiB）。
  每个备份仅在 64 个等距 ROI 内部替换 4KiB，ROI 两翼各 2KiB 保持不变，
  用于验证论文 2.3 的 P1/P2 前提。仅依赖 Python 标准库；固定 `--seed` 保证逐字节可复现。

## 代码布局

- `src/GearHash/` —— `GEAR_TABLE[256]`、`gearRoll()`；**仅用于分块边界**。
- `src/Hash/` —— 基于 OpenSSL EVP 的 SHA-1（`sha1`、`Sha1DigestHash`）；**仅用于去重指纹**。
- `src/BreakingApart/` —— `BoundariesFinder(data, positions, cfg)` 与
  `makeFastCDCConfig(expected, min, ncLevel)`；掩码由期望块长与 NC 等级生成。
- `src/Baselines/` —— `fixedSizeCuts`(FSC/XC)、`gearCuts`(原始 Gear)、`rabinCuts`(48B 窗口)。
- `src/ChunkStore/` —— 全局去重存储：`gChunkPool`（唯一块）+ `gChunkIndex`（SHA-1→Chunk*）；
  `resetStore()` 供多轮实验重置。
- `src/main.cpp` —— 数据集驱动：分块、发射、汇总 `dedupRatio`/`DER`/`avgChunk` 与
  「纯分块 / 全流程」两档吞吐量。

## 注意事项

- 分块参数不再硬编码，`BoundariesFinder` 接收 `BreakingApartConfig`。
- NC 掩码：8KB 的 11/13/15 位用论文公开常量；其他位宽（含 level 1/3）按同样的零填充
  分布生成，属复现假设（见 `paper/REPRODUCTION.md` §9）。
- 去重正确性依赖「SHA-1 分桶 + 逐字节确认」，指纹碰撞不会误判；每个唯一块在内存保留一份内容。
- 去重指纹（SHA-1）与分块哈希（Gear）是两个独立子系统，勿混用。

## 仓库约定

- 提交信息与代码注释使用中文。
- 函数使用尾置返回类型（`auto f(...) -> T`）。
- `results/`、`Dataset/` 未跟踪，勿提交。
