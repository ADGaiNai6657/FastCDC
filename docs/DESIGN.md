# FastCDC 代码设计文档

本文件说明 FastCDC 项目的**代码设计**：目标、总体架构、模块职责、关键设计决策、
参数与不变量。逐文件走查见 [`CODE_REVIEW.md`](CODE_REVIEW.md)，论文理论见
`paper/atc16-paper-xia.pdf`，实验结果与结论见 `paper/REPRODUCTION.md`。

## 1. 目标

复现论文 *FastCDC: a Fast and Efficient Content-Defined Chunking Approach for
Data Deduplication*（USENIX ATC'16）的分块算法，并在此之上做**全局内容去重**：
数据流被切成内容定义的块，相同的块在存储中**只保留一份**，据此计算论文指标：

- 去重率 `dedupRatio = 重复字节 / 总字节 = 1 - uniqueBytes/totalBytes`
- 压缩比 `DER = totalBytes/uniqueBytes`
- 平均块长 `avgChunk = totalBytes/totalChunks`

同时区分**纯分块**与**全流程（分块+去重指纹）**两档吞吐量。

## 2. 总体架构

系统由三个解耦的关注点组成，沿单向数据流串联：

```
文件字节 (std::string)
   │
   ▼
findCuts → BoundariesFinder(data, positions, cfg)   ← src/BreakingApart/（或 Baselines/）
   │  只依据内容产生切点偏移 positions.back()（绝对字节位置，末项 == 文件长度）
   ▼
emitChunk(data, prev, cut)                          ← src/ChunkStore/
   │  切片构造 Chunk（内容副本）
   ▼
chunkStore(std::move(chunk))                        ← src/ChunkStore/
   │  SHA-1 指纹分桶 → equal_range → 长度/逐字节确认
   ├─ 命中：返回既有唯一块指针（不复制、不新增存储）
   └─ 未命中：move 进 gChunkPool，登记 gChunkIndex
   ▼
gChunkPool / gChunkIndex + 统计量（gTotalChunks / gDupChunks / gUniqueBytes …）
```

- **分块**：只看内容决定边界，与存储无关。
- **发射**：把相邻切点之间的切片交给存储。
- **存储**：去重与索引。`gChunkPool`/`gChunkIndex` 跨文件保留，因此同一内容在
  不同文件出现时也会被识别为重复。

## 3. 模块职责

| 目录/文件 | 职责 | 关键符号 |
| --- | --- | --- |
| `src/GearHash/` | 滚动哈希：为**分块边界**提供字节级随机增量 | `GEAR_TABLE`、`gearRoll` |
| `src/Hash/` | **去重指纹**：整块内容的 SHA-1 摘要（OpenSSL EVP） | `sha1`、`Sha1DigestHash` |
| `src/BreakingApart/` | FastCDC 边界查找与参数生成 | `BoundariesFinder`、`makeFastCDCConfig` |
| `src/Baselines/` | 对比基线：固定分块与原始 Gear/Rabin | `fixedSizeCuts`、`gearCuts`、`rabinCuts` |
| `src/ChunkStore/` | 全局去重存储与统计 | `chunkStore`、`emitChunk`、`resetStore` |
| `src/main.cpp` | 数据集驱动：遍历、分块、发射、汇总与 CSV | `Options`、`findCuts`、`processBuffer` |

`GearHash` 与 `Hash` 是两个独立子系统：前者只决定边界，后者只做去重指纹，二者
不可混用（见 §4.1）。

## 4. 关键设计决策

### 4.1 分块哈希与去重指纹分离

- 分块边界用 Gear 滚动哈希（`hash = (hash << 1) + GEAR_TABLE[byte]`），追求速度。
- 去重指纹用 SHA-1（160 位），与论文口径一致；碰撞概率可忽略。
- 历史版本曾复用 64 位 Gear 指纹做去重，现已拆分为 `src/Hash/`，避免两个语义
  不同的哈希互相耦合。

### 4.2 优化哈希判定与零填充掩码

切点条件为 `(hash & Mask) == 0`。掩码低位保留大量 0，使判定实际覆盖更长的滑动
窗口，从而加快滚动哈希的收敛（论文 4.2）。8KB 期望块长的 11/13/15 位掩码直接使用
论文公开常量；其余位宽在 `[16, 49]` 上等距置位生成，保证至少 16 字节窗口。

### 4.3 次最小块切点跳过（MinSize）

从 `last + MinSize` 才开始滚动与判定，跳过不可能产生边界的区间，省去该区间的哈希
计算（论文 4.3）。代价是块长下界抬高，平均块长 ≈ `MinSize + 期望块长`。
`MinSize = 0` 表示不跳过（用于 Figure 11 的 0 档对照）。

### 4.4 归一化分块 NC

当 `NcLevel > 0` 时，块内偏移 `< NormalSize` 使用较难命中的 `MaskS`（base+level 位），
`>= NormalSize` 使用较易命中的 `MaskL`（base-level 位），把块长分布向 `NormalSize`
集中。`NcLevel == 0` 时统一使用 `MaskA`（base 位）。

NC 开启时取 `NormalSize = max(expected, MinSize + expected/2)`：`Min4KB → 8KB`、
`Min8KB → 12KB`，与论文 §5.4「归一化块长 = 4KB + 最小块长」一致，并保证
`NormalSize > MinSize`（否则第一段循环从 `last+MinSize` 起，`MaskS` 永不参与、NC 退化）。
NC 关闭时 `NormalSize = expected`。

### 4.5 MaxSize 强制切分

搜索窗口上界为 `min(last + MaxSize, n)`，若窗口内始终未命中掩码则在该处强制切分，
避免超长块。尾部剩余不足 `MinSize` 时直接作为最后一块切到文件末尾。

### 4.6 指纹分桶 + 逐字节确认

去重用「SHA-1 先分桶、再精确确认」两段式：`unordered_multimap` 按指纹聚合候选，
命中候选后再比较**长度 + 逐字节内容**。指纹碰撞不会把不同内容误判为重复，只多一次
比较，正确性不依赖 SHA-1 的无碰撞假设。

### 4.7 全局跨文件去重

`gChunkPool`/`gChunkIndex` 不随文件清空，只在多轮实验之间由 `resetStore()` 重置，
因此去重是数据集级别的（跨文件），与论文一致。

### 4.8 两档计时

`processBuffer` 仅对 `findCuts` 计时（纯分块）；整轮墙钟时间作为全流程时间。两者
分别换算为 `chunkMBps` / `fullMBps`，用于区分算法速度与去重开销。

## 5. 参数与掩码

`makeFastCDCConfig(expected, min, ncLevel)` 生成配置：
NC 时 `NormalSize = max(expected, min + expected/2)`、非 NC 时 `NormalSize = expected`；
`MaxSize = 8 × expected`；掩码位数 `base = log2(expected)`，
level `L` 前段 `base+L` 位、后段 `base-L` 位。

| 参数 | CLI | 默认值 | 含义 |
| --- | --- | ---: | --- |
| `expectedSize` | `--expected-size N` | 8192 | 期望块长（掩码位宽基准；NC 的 NormalSize 由它和 MinSize 决定） |
| `MinSize` | `--min-size N` | `expected/4 = 2048` | 最小块长；显式 `0` 表示不跳过 |
| `MaxSize` | — | `8×expected = 65536` | 最大块长（强制切分） |
| `ncLevel` | `--nc`(=2) / `--nc-level N` / `--no-nc` | 0（非 NC） | 0=非 NC；1/2/3 归一化等级 |
| `MaskA` | — | base 位（8K 为 `0x0000d90303530000`） | 非 NC |
| `MaskS` | — | base+L 位（8K/L2 为 `0x0003590703530000`） | NC 在 NormalSize 之前 |
| `MaskL` | — | base-L 位（8K/L2 为 `0x0000d90003530000`） | NC 在 NormalSize 之后 |
| `fscSize` | `--fsc-size N` | 10240 | FSC/XC 固定块长 |

对比：非 NC 平均块长 ≈ `MinSize + expectedSize`，NC 更靠近 `NormalSize`。

## 6. 去重存储设计

数据结构（`src/ChunkStore/ChunkStore.h`）：

| 名称 | 说明 |
| --- | --- |
| `ChunkHash` | `Sha1Digest`（20 字节），整块内容的 SHA-1 指纹 |
| `Chunk` | 唯一块：`hash` + 内容副本 `data` + `length` |
| `ChunkRef` | 一次“块出现”：`Chunk*` + 源流内偏移 |
| `gChunkPool` | `deque<Chunk>`，唯一块池；`push_back` 后既有指针不失效 |
| `gChunkIndex` | `unordered_multimap<ChunkHash, Chunk*, Sha1DigestHash>`，指纹→块指针 |
| `vChunks` | 每个输入流一组 `ChunkRef`（轻量引用） |
| 统计量 | `gTotalChunks` / `gDupChunks` / `gTotalBytes` / `gUniqueBytes` |

`chunkStore(chunk)` 流程：
1. `sha1` 求整块 160 位指纹；
2. 计一次“发射”（`++gTotalChunks`、累加 `gTotalBytes`）；
3. `equal_range(hash)` 取全部同指纹候选；
4. 逐个「长度 + 逐字节」确认，命中即 `++gDupChunks` 并返回既有指针；
5. 全部不符才 move 进 `gChunkPool`，登记索引并累加 `gUniqueBytes`。

`emitChunk(data, begin, end)` 构造 `Chunk`（复制 `[begin, end)`）→ `chunkStore` →
把 `ChunkRef` 追加到 `vChunks.back()`；`beginStream()` 为每个文件开一组出现记录。

## 7. 不变量与正确性

对 `chunkStore` 调用 N 次后恒成立：

- `gTotalChunks == gChunkPool.size() + gDupChunks`（发射 = 唯一 + 重复）
- `gTotalBytes == 输入总字节数`
- `gUniqueBytes == gChunkPool` 中所有块内容总字节数
- `dedupRatio == 1 - gUniqueBytes / gTotalBytes`
- `DER == gTotalBytes / gUniqueBytes`

去重正确性由「指纹分桶 + 逐字节确认」保证；指纹碰撞不会导致误判或数据丢失。

## 8. 与论文的对应与差异

| 论文 | 本项目 | 备注 |
| --- | --- | --- |
| Gear 滚动哈希 | `GearHash` | 一致 |
| 优化哈希判定 `fp & Mask == 0` | `BoundariesFinder` | 一致 |
| 次最小块切点跳过 | 从 `last+MinSize` 起算 | 一致 |
| 归一化分块 NC | `maskFor()` 按偏移切换掩码 | 默认关闭（level 0），`--nc` 启用 level 2 |
| MaxSize 强制切分 | `searchEnd` 上限 | 一致 |
| 去重指纹 SHA-1 | OpenSSL EVP SHA-1 | 一致 |
| 参数化/多期望块长 | `makeFastCDCConfig` + CLI | 已完成 |
| 基线 RC/GC/XC | `src/Baselines/` | 已完成（AE 未做） |

更完整的数值与趋势对照见 `paper/REPRODUCTION.md`。

## 9. 已知限制与后续

- `gChunkPool` 为每个唯一块保留一份内容副本，大数据集内存占用为 O(uniqueBytes)。
- 论文使用的 Gear 随机表未公开；当前为自生成随机表，绝对数值有系统偏差。
- 本地数据集仅覆盖 LNX（`DataSet_2`）、TAR（`DataSet_3`）、SYN（`DataSet_4`），
  WEB/VMA/VMB/RDB 缺失，规模为论文的缩比。
- 基线已实现 FSC/Gear/Rabin；AE 与 Table 6（instructions/IPC/cycles）未做。
- 全局状态非线程安全，当前为单线程设计。
- 代码中仍有历史遗留的未用 API（`updateGearHash`、`isExist`、`lookup`），详见
  [`CODE_REVIEW.md`](CODE_REVIEW.md)。

## 10. 相关文档

- 逐文件走查与审查发现：[`docs/CODE_REVIEW.md`](CODE_REVIEW.md)
- 实验复现结果与结论：`paper/REPRODUCTION.md`
- 论文原文：`paper/atc16-paper-xia.pdf`
