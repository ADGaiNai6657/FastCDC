# FastCDC 论文实验复现进度

对照 `paper/atc16-paper-xia.pdf`（USENIX ATC'16，Xia 等）与当前代码，
记录已完成 / 待完成的工作，并给出 todo list。

## 1. 论文实验总览

论文 FastCDC 的三项核心技术：优化哈希判定（4.2）、次最小块切点跳过（4.3）、
归一化分块 NC（4.4）；实验在第 5 章。

| 论文章节 | 图表 | 内容 |
| --- | --- | --- |
| 5.1 | Table 2 | 实验平台、基线、指标与 7 个数据集（TAR/LNX/WEB/VMA/VMB/RDB/SYN，共约 5TB） |
| 5.2 | Figure 10, Table 3 | 优化哈希判定：与 Rabin / Gear / AE 的速度对比；去重率与平均块长随期望块长（4K/8K/16K）变化 |
| 5.3 | Figure 11 | 切点跳过：不同 MinSize（2K/4K/8K）对速度、去重率、平均块长的影响 |
| 5.4 | Figure 12 | 归一化分块：NC level 1/2/3（掩码位 (14,12)/(15,11)/(16,10)）的敏感性 |
| 5.5 | Table 4/5/6, Figure 13 | 综合对比 RC-MIN2KB、FC-MIN2KB、FC-NC-MIN4KB、FC-NC-MIN8KB、XC(10KB) |

论文 Algorithm 1（FastCDC8KB）参数：
`MaskS=0x0003590703530000`（15 个 1）、`MaskA=0x0000d90303530000`（13 个 1）、
`MaskL=0x0000d90003530000`（11 个 1）；`MinSize=2KB`、`MaxSize=64KB`、
`NormalSize=8KB`，从 `i=MinSize` 起 `fp=0` 开始滚动。

## 2. 现有代码与论文的对应

| 论文技术/条目 | 代码位置 | 状态 |
| --- | --- | --- |
| Gear 滚动哈希 `fp=(fp<<1)+G[b]` | `src/GearHash/GearHash.h:gearRoll` | 已完成 |
| 掩码判定 `(fp & Mask)==0`（4.2 简化） | `src/BreakingApart/BreakingApart.cpp` | 已完成 |
| 零填充掩码 MaskS/MaskA/MaskL | `BreakingApart.cpp` 常量 | 已完成（数值与论文一致） |
| 次最小块切点跳过（4.3） | `BoundariesFinder` 从 `last+MinSize` 起 `hash=0` | 已完成 |
| 归一化分块 NC（4.4，level 2） | `maskFor()`：NormalSize 前 `MaskS`、后用 `MaskL` | 已完成 |
| 非 NC 模式（用 MaskA） | `is_NC=false` 时用 `MaskM` | 已完成 |
| MaxSize 强制切分 | `searchEnd = min(last+MaxSize, n)` | 已完成 |
| 文件读取 + min/avg/max 输出 | `src/main.cpp` | 已完成（单文件） |

当前默认参数：`MinSize=4048(≈4KB)`、`NormalSize=8096(8KB)`、
`MaxSize=64768(≈64KB)`，NC 掩码 15/11 位（即 NC level 2）。
等价于论文中综合性能较好的 **FC-NC-MIN4KB**。

## 3. 已完成

- FastCDC 核心分块算法：Gear 哈希、优化哈希判定、切点跳过、NC、MaxSize。
- 单文件 CLI：`FastCDC <file> [--nc]`，输出块数、min/avg/max 块长。
- 可编译、无警告（`-Wall -Wextra`），CMake 构建通过。
- 本地可用数据：`Dataset/DataSet_1`（5 个 emacs tar.gz，TAR 类）、
  `Dataset/DataSet_2`（4 个 emacs 版本源码，LNX 类，含版本差异可用于去重）。

## 4. 待完成

### 4.1 参数化与 CLI（优先级高）

- [ ] `BoundariesFinder` 接收 `BreakingApartConfig` 参数，去掉文件级全局配置。
- [ ] 期望块长可配（4K/8K/16K），据此推导 Min/Normal/Max 与掩码。
- [ ] MinSize 可配（0/2K/4K/8K）以复现 Figure 11。
- [ ] NC 开关 + level 1/2/3（掩码位对 (14,12)/(15,11)/(16,10)），复现 Figure 12。
- [ ] 兼容论文 Algorithm 1 的默认（MinSize=2KB）。

### 4.2 评价指标（优先级高）

- [ ] 分块吞吐量测速（MB/s，多次运行取平均），复现 Figure 10/13。
- [ ] 数据集级平均块长 = 总字节 / 总块数（跨文件聚合）。
- [ ] 去重率 = 重复数据量 / 总数据量：需对每个块做 SHA1 指纹并全局去重统计。
      （当前完全缺失，是 Table 3/4 的关键指标。）

### 4.3 基线算法（优先级高，Table 3 / Fig 10 / 13 需要）

- [ ] Fixed-Size Chunking（XC，10KB）—— 最简单。
- [ ] Gear-based CDC（未做零填充/简化的原始 Gear）—— 证明低滑动窗口导致去重率下降。
- [ ] Rabin-based CDC（48 字节窗口，LBFS 配置）—— 论文主基线。
- [ ] AE-based CDC（非对称极值）。

### 4.4 数据集与批处理框架

- [ ] 遍历数据集目录、逐个文件分块并聚合统计（当前仅单文件）。
- [ ] 数据集缺口：本地只有 TAR/LNX 类；WEB/VMA/VMB/RDB/SYN 缺失，
      完整 Table 3-6 无法 1:1 复现，只能做同规模缩比验证。
- [ ] 输出统一的表格/CSV，便于与论文 Table 3-6 对照。

### 4.5 性能剖析（Table 6，可选）

- [ ] 统计指令数、IPC、CPU cycles（`perf stat`），对比 RC/FC/FC-NC。

### 4.6 其他

- [ ] `updateGearHash` 目前未被使用，可保留作通用接口或删除。
- [ ] 论文未公开作者使用的 Gear 随机表；当前为自生成随机表，
      统计结果可能与论文有细微差异，需在报告中说明。

## 5. Todo List（按阶段）

### 阶段 0：核心算法（已完成）
- [x] Gear 滚动哈希
- [x] 优化哈希判定 `(fp & Mask) == 0`
- [x] 切点跳过 + 归一化分块 + MaxSize
- [x] 单文件 min/avg/max 输出

### 阶段 1：参数化
- [ ] `BoundariesFinder` 改为接受 `BreakingApartConfig`
- [ ] CLI 支持 `--expected-size`、`--min-size`、`--nc-level`、`--no-nc`
- [ ] 由期望块长自动生成掩码，并补齐 NC level 1/3 的掩码

### 阶段 2：指标
- [ ] 吞吐量测速
- [ ] 数据集级平均块长聚合
- [ ] SHA1 指纹 + 去重率计算

### 阶段 3：基线
- [ ] 实现 FSC(10KB)
- [ ] 实现原始 Gear-based CDC
- [ ] 实现 Rabin-based CDC（48B 窗口）
- [ ] （可选）实现 AE-based CDC

### 阶段 4：实验复现
- [ ] Figure 10 / Table 3：速度与去重率 vs 期望块长（TAR/LNX 类）
- [ ] Figure 11：MinSize 敏感性
- [ ] Figure 12：NC level 敏感性
- [ ] Table 4/5：综合去重率与平均块长
- [ ] Table 6 / Figure 13：速度与 CPU 开销
- [ ] 撰写复现报告，标注与论文数值的差异及原因

## 6. 复现注意事项

- 论文平台：Ubuntu 12.04、Intel i7-4770 @3.4GHz、16GB RAM、GCC 4.7.3 `-O3`；
  本机为 macOS/Apple Silicon，绝对速度不可直接对比，应关注相对倍数与趋势。
- 论文默认 `MaxSize = 8×期望块长`、`MinSize = 1/4×期望块长`（LBFS 配置），
  但 Algorithm 1 与 5.4/5.5 的 NC 实验使用 `MinSize=2K/4K/8K`，需区分。
- 去重率对块边界极敏感；复现前应先验证边界确定性与跨块一致性。
