# FastCDC 项目设计文档

本文件讲解 FastCDC 项目的整体设计：模块划分、数据流、关键算法与去重存储。
算法理论见 `paper/atc16-paper-xia.pdf`，复现结果与结论见 `paper/REPRODUCTION.md`，
历史实验进度见 `paper/EXPERIMENTS.md`。

## 1. 目标

复现论文 *FastCDC: a Fast and Efficient Content-Defined Chunking Approach for
Data Deduplication*（USENIX ATC'16）的分块算法，并在此之上做**全局内容去重**：
数据流被切成内容定义的块，相同的块在存储中**只保留一份**，用于计算论文指标：
去重率 `dedupRatio = 重复字节 / 总字节 = 1 - uniqueBytes/totalBytes`，
压缩比 `DER = totalBytes/uniqueBytes`，以及平均块长。

## 2. 数据流总览

```
文件字节 (std::string)
   │
   ▼
findCuts → BoundariesFinder(data, positions, cfg)  ← src/BreakingApart/（或 Baselines/）
   │  产生切点偏移 positions.back()（绝对字节位置，末项 == 文件大小）
   ▼
emitChunk(data, prev, cut)                    ← src/ChunkStore/
   │  切片构造 Chunk（内容副本）
   ▼
chunkStore(std::move(chunk))                  ← src/ChunkStore/
   │  SHA-1 指纹分桶 → equal_range → 逐字节确认
   ├─ 命中：返回既有唯一块指针（不复制、不新增存储）
   └─ 未命中：move 进 gChunkPool，登记 gChunkIndex
   ▼
gChunkPool / gChunkIndex + 统计量（gTotalChunks / gDupChunks / gUniqueBytes …）
```

三个关注点彼此解耦：**分块**只看内容决定边界；**发射**负责把边界间切片交给
存储；**存储**负责去重与索引。`gChunkPool`/`gChunkIndex` 跨文件保留，
因此同一内容在不同文件出现时也会被识别为重复。

## 3. 目录结构

```
src/
├── GearHash/        # 滚动哈希：GEAR_TABLE 与 gearRoll / updateGearHash
├── Hash/            # SHA-1 去重指纹：sha1 / Sha1DigestHash（OpenSSL EVP）
├── BreakingApart/   # FastCDC 边界查找：BoundariesFinder / makeFastCDCConfig
├── Baselines/       # 基线：fixedSizeCuts / gearCuts / rabinCuts
├── ChunkStore/      # 全局去重存储：chunkStore / emitChunk / resetStore
└── main.cpp         # 数据集驱动：遍历、分块、发射、汇总指标
paper/
├── atc16-paper-xia.pdf
├── EXPERIMENTS.md   # 论文实验复现进度（历史）
└── REPRODUCTION.md  # 本轮复现报告（结果与结论）
docs/DESIGN.md       # 本文
```

## 4. 模块设计

### 4.1 GearHash（`src/GearHash/`）

Gear 哈希是 FastCDC 的速度来源：用一张 256 项随机 64 位表把字节映射成哈希增量。

- `GEAR_TABLE[256]`：`static constexpr` 随机表（`GearHash.h`）。
- `gearRoll(hash, byte)`：头文件内联，`hash = (hash << 1) + GEAR_TABLE[byte]`。
  左移使旧字节随滚动逐渐移出有效位，加法引入新字节。
- `updateGearHash(data, length, hash)`：把 `length` 个字节依次滚入 `hash`，
  由调用者持有 `hash` 状态（`GearHash.cpp`）。

Gear 哈希**仅用于分块边界**（`BoundariesFinder`）。去重指纹改用 `src/Hash/` 的
SHA-1（下节），两者互不影响。

### 4.1.1 Hash（`src/Hash/`）

基于 OpenSSL EVP 的 SHA-1 封装：`sha1()` 对整块内容求 160 位摘要作为去重 key，
`Sha1DigestHash` 把摘要压成 `unordered_multimap` 桶号。与论文 SHA-1 指纹口径一致。

### 4.2 BreakingApart（`src/BreakingApart/`）

`BoundariesFinder(data, positions, cfg)` 实现论文 Algorithm 1 的 FastCDC：

- **MinSize 跳过**：从 `last + MinSize` 才开始滚动哈希，跳过不可能产生边界的区域，
  省去该区间的哈希计算（论文 4.3）。
- **优化哈希判定**：命中条件为 `(hash & Mask) == 0`，掩码带零填充以放大滑动窗口
  （论文 4.2）。
- **归一化分块 NC**（论文 4.4）：`cfg.NcLevel > 0` 时，块内偏移 `< NormalSize`
  用较难命中的 `MaskS`、`>= NormalSize` 用较易命中的 `MaskL`，使块长分布向
  目标区间集中；为 0 时统一用 `MaskA`。
- **MaxSize 强制切分**：搜索窗口不超过 `min(last + MaxSize, n)`，未命中掩码则
  在该处强制切分，避免产生超长块。

参数由 `makeFastCDCConfig(expected, min, ncLevel)` 生成并传入 `BoundariesFinder`：
`NormalSize=expected`、`MaxSize=8×expected`；掩码位数 base = log2(expected)，
NC level L 前段 base+L 位、后段 base-L 位。8KB 的 11/13/15 位掩码使用论文公开
常量，其余位宽按同样零填充分布生成（见 `paper/REPRODUCTION.md` §9）。

切点以绝对字节偏移写入 `positions.back()`，最后一项为数据长度。

### 4.3 ChunkStore（`src/ChunkStore/`）

去重存储移植自 Bimodal 项目，核心是「指纹分桶 + 内容确认」两段式判重。

数据结构（`ChunkStore.h`）：

| 名称 | 说明 |
| --- | --- |
| `ChunkHash` | `Sha1Digest`（20 字节），整块内容的 SHA-1 指纹 |
| `Chunk` | 唯一块：`hash` + 内容副本 `data` + `length` |
| `ChunkRef` | 一次“块出现”：`Chunk*` + 源流内偏移 |
| `gChunkPool` | `deque<Chunk>`，唯一块池，`push_back` 后既有指针不失效 |
| `gChunkIndex` | `unordered_multimap<ChunkHash, Chunk*, Sha1DigestHash>`，指纹→块指针 |
| `vChunks` | 每个输入流一组 `ChunkRef`（轻量引用） |
| 统计量 | `gTotalChunks` / `gDupChunks` / `gTotalBytes` / `gUniqueBytes` |

核心流程 `chunkStore(chunk)`（`ChunkStore.cpp`）：

1. 用 `sha1` 对整块内容求 160 位指纹；
2. 计一次“发射”（`++gTotalChunks`，累加 `gTotalBytes`）；
3. `gChunkIndex.equal_range(hash)` 取出全部同指纹候选；
4. 逐个「长度 + 逐字节」确认，命中即 `++gDupChunks` 并返回既有指针；
5. 全部不符才 `std::move` 进 `gChunkPool`，登记索引并累加 `gUniqueBytes`。

**为什么仍用 multimap + 逐字节确认**：SHA-1 碰撞概率可忽略，逐字节确认只作兜底。
若只按指纹判重，碰撞会把不同内容误判为重复而**丢失数据**。因此用指纹快速分桶，
再用逐字节比较给出精确定论——碰撞不影响正确性，只多一次比较。

`emitChunk(data, begin, end)` 负责切片发射：构造 `Chunk`（复制 `[begin, end)`），
调用 `chunkStore`，并把 `ChunkRef` 追加到 `vChunks.back()`。`beginStream()` 为每个
文件开一组出现记录。

### 4.4 main 驱动（`src/main.cpp`）

- 解析参数：`[<file|dir>] [--algo fastcdc|gear|rabin|fsc] [--expected-size N]`
  `[--min-size N] [--nc|--nc-level N|--no-nc] [--fsc-size N] [--runs N] [--csv P]`
  `[--recursive|-r] [--quiet]`；无路径时默认 `Dataset/`（递归）。
- 目录遍历后**排序**，保证备份流按版本先后处理。
- 逐文件整读为二进制缓冲，调用对应分块器，对每个切点区间 `emitChunk`。
- 汇总输出：`totalChunks`、`uniqueChunks`、`duplicateChunks`、`totalBytes`、
  `uniqueBytes`、`dedupRatio`（论文定义）、`DER`、`avgChunk`、`minChunk`、`maxChunk`、
  `chunkMBps`（纯分块）、`fullMBps`（含去重）。`--runs` 取多次平均，`--csv` 追加行。

## 5. 不变量与正确性

对 `chunkStore` 调用 N 次后恒成立：

- `gTotalChunks == gChunkPool.size() + gDupChunks`（发射 = 唯一 + 重复）
- `gTotalBytes == 输入总字节数`
- `gUniqueBytes == gChunkPool` 中所有块内容总字节数
- `dedupRatio == 1 - gUniqueBytes / gTotalBytes`（论文去重率）
- `DER == gTotalBytes / gUniqueBytes`（压缩比）

去重正确性由「指纹分桶 + 逐字节确认」保证；指纹碰撞不会导致误判。

## 6. 参数与掩码

| 参数 | 默认值 | 含义 |
| --- | ---: | --- |
| `expectedSize` | 8192 | 期望块长（NC 的归一化中心 NormalSize） |
| `MinSize` | expected/4 = 2048 | 最小块长（切点跳过区间，可设 0） |
| `MaxSize` | 8×expected = 65536 | 最大块长（强制切分） |
| `ncLevel` | 0 | 0=非 NC；1/2/3 归一化等级 |
| `MaskA` | 由 base 位生成（8K 为 0x0000d90303530000） | 非 NC |
| `MaskS` | base+lv 位（8K/lv2 为 0x0003590703530000） | NC 在 NormalSize 之前 |
| `MaskL` | base-lv 位（8K/lv2 为 0x0000d90003530000） | NC 在 NormalSize 之后 |

对比：非 NC 的平均块长 ≈ `MinSize + expectedSize`，NC 更靠近 `NormalSize`。

## 7. 构建与运行

```bash
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release
./cmake-build-release/FastCDC Dataset/DataSet_2 --recursive --nc
```

依赖 OpenSSL（去重 SHA-1），由 CMake 自动探测 keg-only 安装。快速编译：

```bash
g++ -std=c++20 -O3 -Isrc src/main.cpp src/GearHash/GearHash.cpp src/Hash/Hash.cpp \
    src/BreakingApart/BreakingApart.cpp src/Baselines/Baselines.cpp \
    src/ChunkStore/ChunkStore.cpp -lcrypto -o fastcdc
```

## 8. 与论文的对应与差异

| 论文 | 本项目 | 备注 |
| --- | --- | --- |
| Gear 滚动哈希 | `GearHash` | 一致 |
| 优化哈希判定 `fp & Mask == 0` | `BoundariesFinder` | 一致 |
| 次最小块切点跳过 | 从 `last+MinSize` 起算 | 一致 |
| 归一化分块 NC | `maskFor()` 按偏移切换掩码 | 默认 NC level 2 |
| MaxSize 强制切分 | `searchEnd` 上限 | 一致 |
| 去重指纹 SHA-1 | OpenSSL EVP SHA-1（复用 Bimodal） | 一致 |
| 参数化/多期望块长 | `makeFastCDCConfig` + CLI | 已完成 |
| 基线 RC/GC/XC | `src/Baselines/` | 已完成（AE 未做） |

## 9. 已知限制与后续

- `gChunkPool` 为每个唯一块保留一份内容副本，大数据集需注意内存占用。
- 论文使用的 Gear 随机表未公开；当前为自生成随机表，绝对数值有系统偏差。
- 本地数据集仅覆盖 LNX（`DataSet_2`）、TAR（`DataSet_3`）、SYN（`DataSet_4`），
  WEB/VMA/VMB/RDB 缺失，规模为论文的缩比。
- 基线已实现 FSC/Gear/Rabin；AE 与 Table 6（instructions/IPC/cycles）未做。
- 结果与趋势对照见 `paper/REPRODUCTION.md`。
