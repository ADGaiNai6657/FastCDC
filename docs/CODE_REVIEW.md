# FastCDC 代码从头审查

本文档面向**从零开始的代码审查**：按依赖自底向上的顺序逐个文件走查，说明每个
符号的职责与逻辑、核对数据流与不变量，并列出审查发现（死代码、边界、可改进项）。

设计意图见 [`DESIGN.md`](DESIGN.md)；论文理论见 `paper/atc16-paper-xia.pdf`。

## 1. 审查范围与方法

- 审查对象：当前工作树，`src/` 全部源文件与 `CMakeLists.txt`（6 个模块、1 个目标）。
- 阅读顺序（被依赖者在前）：`GearHash` → `Hash` → `BreakingApart` → `Baselines`
  → `ChunkStore` → `main` → `CMakeLists`。
- 关注点：功能正确性、边界条件、内存/性能、接口契约、无效或冗余代码。

### 结论速览

| 模块 | 职责 | 状态 | 备注 |
| --- | --- | --- | --- |
| `GearHash` | 分块滚动哈希 | 正常 | `updateGearHash` 为死代码 |
| `Hash` | SHA-1 去重指纹 | 正常 | thread_local 上下文不释放 |
| `BreakingApart` | FastCDC 边界 + 参数 | 正常 | 接口用 `.back()` 传回结果 |
| `Baselines` | FSC/Gear/Rabin 基线 | 正常 | — |
| `ChunkStore` | 全局去重存储 | 正常 | `isExist`/`lookup`/`vChunks` 无调用 |
| `main` | 驱动与统计 | 正常 | 数字解析未捕获异常 |

## 2. 逐文件走查

### 2.1 GearHash（`src/GearHash/`）

- `GEAR_TABLE[256]`（`GearHash.h:11`）：`static constexpr` 的 256 项 64 位随机表。
- `gearRoll(hash, byte)`（`GearHash.h:79`）：头文件内联，
  `hash = (hash << 1) + GEAR_TABLE[byte]`。左移让旧字节逐渐移出有效位，加法引入新字节。
- `updateGearHash(data, length, hash)`（`GearHash.h:84` / `GearHash.cpp:8`）：把
  `length` 个字节依次滚入由调用者持有的 `hash`。

**审查发现**
- `updateGearHash` **无任何调用者**：分块处直接内联调用 `gearRoll`
  （`BreakingApart.cpp:100`、`Baselines.cpp:53`），指纹改用 SHA-1 后此函数失去用途。
  属历史遗留死代码，可删除（连同 `GearHash.cpp` 或保留作工具函数）。
- `static constexpr` 具内部链接：`main.cpp`、`BreakingApart.cpp`、`Baselines.cpp`
  各自包含头文件，故每个 TU 各持一份 `GEAR_TABLE`（约 2 KiB），链接期不合并。
  改 `inline constexpr`（C++17）可只保留一份；非正确性问题。

### 2.2 Hash（`src/Hash/`）

- `Sha1Digest = std::array<uint8_t,20>`（`Hash.h:17`）：160 位摘要。
- `sha1(data)`（`Hash.h:20` / `Hash.cpp:26`）：`sha1Into` 用 thread_local 的
  `EVP_MD_CTX` 复用上下文，`Init/Update/Final` 输出 20 字节。
- `Sha1DigestHash::operator()`（`Hash.h:24` / `Hash.cpp:33`）：`memcpy` 摘要前 8 字节
  作为桶号。

**审查发现**
- `thread_local EVP_MD_CTX*` 只分配、不释放（线程退出时不 `EVP_MD_CTX_free`），
  属可接受的微量泄漏；若需洁癖可加 RAII 包装。
- `EVP_*` 返回值未检查；实际失败（如初始化失败）概率极低，但不返回错误的代价是
  得到全零摘要 → 可能造成错误去重。属加固项。
- 桶号取前 8 字节，结果依赖机器字节序；但无序容器只要求「同 key 同值」，
  不要求跨平台一致，**不构成问题**。

### 2.3 BreakingApart（`src/BreakingApart/`）

- 掩码常量（`BreakingApart.cpp:18-20`）：`MASK_A_13`/`MASK_S_15`/`MASK_L_11`，与
  论文 Algorithm 1 的公开常量一致。
- `buildMask(bits)`（`BreakingApart.cpp:24`）：`bits` 为 11/13/15 时返回论文常量；
  否则在 `[16, 49]` 等距置 `bits` 个 1，低位留零以保证窗口 ≥ 16 字节。
- `maskFor(cfg, chunkOffset, isNC)`（`BreakingApart.cpp:47`）：非 NC 返回 `MaskA`；
  NC 按 `chunkOffset < NormalSize` 返回 `MaskS`/`MaskL`。
- `makeFastCDCConfig(expected, min, ncLevel)`（`BreakingApart.cpp:56`）：
  `base = floor(log2(expected))`，`level = clamp(ncLevel,0,3)`；
  `MaskA=buildMask(base)`，`MaskS=buildMask(base+level)`，`MaskL=buildMask(base-level)`；
  `NormalSize = level>0 ? max(expected, min + expected/2) : expected`（论文 §5.4，由
  `docs/PAPER_CONFORMANCE.md` FIX-2 修正，保证 `NormalSize > MinSize`）；
  返回 `{min, normalSize, expected*8, maskA, maskS, maskL, level}`。
- `BoundariesFinder(data, vPosition, cfg)`（`BreakingApart.cpp:71`）：
  1. `vPosition.emplace_back()` 追加一组切点，取引用 `cuts`；
  2. `last` 为当前块起点；剩余 `n-last <= MinSize` 时切到 `n` 并结束；
  3. `searchEnd = min(last+MaxSize, n)`，默认切点 `cut = searchEnd`；
  4. 从 `i = last+MinSize` 滚动到 `searchEnd`，命中 `(hash & maskFor(...)) == 0`
     则 `cut=i` 并跳出；
  5. 压入 `cut`，`last = cut`。

**审查发现**
- 边界正确：`MinSize=0` 时不跳过（循环从 `last` 开始），合法；`n <= MinSize` 整文件
  单块；非 2 的幂期望值经 `floor(log2)` 取较小位宽，属设计选择而非缺陷。
- 结果通过 `vPosition.back()` 传回，接口略隐晦；`main` 每次新建 `positions` 且只取
  `.back()`，用法自洽，但可考虑改为直接返回 `vector<size_t>` 更清晰。
- `maskFor` 的 `chunkOffset = i - last` 语义正确（块内相对偏移）。

### 2.4 Baselines（`src/Baselines/`）

- `fixedSizeCuts(data, chunkSize, cuts)`（`Baselines.cpp:13`）：每 `chunkSize` 切一刀，
  末尾补 `n`；空输入直接返回。
- `gearCuts(data, minSize, maxSize, expectedSize, cuts)`（`Baselines.cpp:26`）：用
  `base = floor(log2(expected))` 的**连续低位掩码** `(1<<base)-1`（无零填充），
  外层框架与 FastCDC 相同的「MinSize 跳过 + MaxSize 强切」。
- `rabinCuts(data, minSize, maxSize, expectedSize, cuts)`（`Baselines.cpp:64`）：48 字节滑窗，
  多项式 `0x3DA3358B4DC173`，除数 `D = expectedSize`（`mask = expectedSize-1`），
  阈值 `r = min(0x78, expectedSize-1)`；用
  `fp = fp*p + b - old*p^window`（mod 2^64）滚动。

**审查发现**
- 除数 `D` 随 `expectedSize` 缩放（论文 §5.1，`docs/PAPER_CONFORMANCE.md` FIX-1），
  `expectedSize < 2` 时直接返回，避免掩码为 0 恒命中。
- Rabin 先滚入 `min(last+window, searchEnd)` 个字节初始化窗口；随后 `i-last>=minSize`
  才判定，保证边界不早于 MinSize；`pPow = p^48` 由循环累乘得到，与滚动式一致。
- 三种基线共用「min 跳过 + max 强切」外层逻辑，只有判定不同，便于与 FastCDC 同口径
  对比。空输入均返回空切点。

### 2.5 ChunkStore（`src/ChunkStore/`）

- 全局状态（`ChunkStore.h:43-51`）：`gChunkPool`（deque）、`gChunkIndex`
  （unordered_multimap）、`gTotalChunks/gDupChunks/gTotalBytes/gUniqueBytes`、`vChunks`。
- `chunkStore(chunk)`（`ChunkStore.cpp:37`）：求 SHA-1 → `++gTotalChunks`、
  `gTotalBytes += length` → `equal_range` → 逐候选「长度 + `data`」确认，命中则
  `++gDupChunks` 返回既有指针 → 未命中 move 入池、登记索引、累加 `gUniqueBytes`。
- `emitChunk`（`ChunkStore.cpp:85`）：跳过 `end<=begin`，构造 `Chunk`，
  `chunkStore` 后把 `ChunkRef{stored, begin}` 追加到 `vChunks.back()`。
- `beginStream`（`:21`）、`resetStore`（`:26`）、`isExist`（`:67`）、`lookup`（`:72`）。

**审查发现**
- 不变量成立：`gTotalChunks == gChunkPool.size() + gDupChunks`，因为重复块不新增池项、
  未命中块只新增一项。
- `emitChunk` 依赖调用方先执行 `beginStream()`，否则 `vChunks.back()` 是 UB；契约写在
  `ChunkStore.h` 注释中，`main.cpp:99` 遵守。
- **无外部调用**：`isExist`、`lookup` 及 `vChunks`（只写不读）当前均无消费者。
  `vChunks` 疑似为将来「按出现位置读取切片」预留；`isExist`/`lookup` 为查重 API 的
  未用部分。属可裁剪或标注的冗余。
- 每个唯一块复制一份完整内容，内存 O(uniqueBytes)；全局状态非线程安全（当前单线程）。

### 2.6 main（`src/main.cpp`）

- `Options`（`:28`）：默认 `algo=FastCDC`、`expectedSize=8192`、`ncLevel=0`、
  `fscSize=10240`、`runs=1`。
- `findCuts`（`:59`）：按 `algo` 分派。FastCDC 用 `BoundariesFinder` 取
  `positions.back()`；Gear 传 `cfg.MinSize/MaxSize/expectedSize`；Rabin 传
  `min = minSizeSet ? minSize : expected/4`、`max = expected*8`、`expected`（除数 D）；FSC 用 `fscSize`。
- `processBuffer`（`:88`）：只对 `findCuts` 计时（纯分块），随后 `beginStream()` +
  逐切点更新 `gMinChunk/gMaxChunk` 并 `emitChunk`。
- `datasetRoot`（`:150`）：优先 `../Dataset` 再 `Dataset`，适配从构建目录运行。
- `parseArgs`（`:175`）：`--algo/--expected-size/--min-size/--nc/--nc-level/--no-nc/
  --fsc-size/--recursive|-r/--runs/--csv/--quiet`；非 `-` 开头视为路径。
- `main`（`:237`）：无路径时默认 `Dataset/` 且递归；`minSize==0 && !minSizeSet` 时兜底
  为 `expected/4`；构建 `cfg`；`--runs` 每轮 `resetStore()` 后重跑；汇总输出与 CSV。

**审查发现**
- `:76` 的 `opts.minSizeSet ? opts.minSize : opts.expectedSize/4` 是**冗余**：`main`
  在 `:248-250` 已把未显式设置的 `minSize` 兜底为 `expected/4`，两种写法结果相同。
- 计时口径正确：`chunkSeconds` 只累计 `findCuts`；`fullSeconds` 覆盖从 `runStart`
  到一轮结束（含发射与去重）。
- `--runs>1` 时统计量取自最后一轮，算法确定性下等价，时间取多轮平均。
- `std::stoull`/`std::stoi`（`:193`/`:201`/`:211`）未捕获异常：传入非数字参数会
  `std::terminate`。属健壮性问题，可改为捕获并报错。
- CSV 行按逗号拼接，未对路径中的逗号/换行转义；低风险。
- `perFile` 仅在 `verbose && files.size()<=32 && runs==1` 时逐文件打印，避免刷屏。

### 2.7 CMakeLists.txt

- `cmake_minimum_required(VERSION 4.1)`；C++20；未指定构建类型时默认 `Release`。
- `find_package(OpenSSL REQUIRED)`，链接 `OpenSSL::Crypto`；Apple 下探测 keg-only 路径。
- 单一可执行目标 `FastCDC`，包含全部源文件。

**审查发现**
- 未启用 `-Wall -Wextra`（早期文档曾声称已启用），建议加入以获得编译期告警。

## 3. 跨切面核对

### 3.1 数据流

`readFile` 整读文件 → `findCuts` 得切点 → `emitChunk` 逐片 → `chunkStore` 去重 →
统计量。`beginStream`/`resetStore` 的生命周期与 `--runs` 匹配。

### 3.2 不变量

| 不变量 | 位置 | 核对 |
| --- | --- | --- |
| `gTotalChunks == gChunkPool.size() + gDupChunks` | `ChunkStore.cpp:42-63` | 成立 |
| `gTotalBytes == 输入总字节` | `ChunkStore.cpp:43` | 成立（每块计长） |
| `gUniqueBytes == Σ 唯一块长度` | `ChunkStore.cpp:62` | 成立 |
| `dedupRatio == 1 - unique/total` | `main.cpp:313` | 成立 |
| `DER == total/unique` | `main.cpp:316` | 成立 |

### 3.3 并发与内存

- 全为单线程全局状态；`hash` 为 `thread_local`，但 `ChunkStore` 参数非线程安全。
- `gChunkPool` 持有全部唯一块内容，内存随去重后数据集大小线性增长。

### 3.4 性能

- 热循环（`BoundariesFinder`/`gearCuts`/`rabinCuts`）逐字节内联 `gearRoll`，无函数
  调用与分配；`findCuts` 计时只覆盖分块，避免去重干扰算法速度。

## 4. 审查发现汇总

| # | 位置 | 类型 | 严重度 | 说明 |
| --- | --- | --- | --- | --- |
| 1 | `GearHash.h:84` / `.cpp:8` | 死代码 | 中 | `updateGearHash` 无调用者 |
| 2 | `ChunkStore.cpp:67,72` | 死代码 | 中 | `isExist`/`lookup` 无调用者 |
| 3 | `ChunkStore.cpp:91` | 冗余 | 低 | `vChunks` 只写不读 |
| 4 | `main.cpp:76` | 冗余 | 低 | Rabin 的 min 三元与 `:248` 兜底重复 |
| 5 | `GearHash.h:11` | 内存 | 低 | `static constexpr` 表按 TU 重复，可 `inline` |
| 6 | `Hash.cpp:16` | 健壮性 | 低 | thread_local `EVP_MD_CTX` 不释放、返回值未检 |
| 7 | `main.cpp:193` | 健壮性 | 低 | `stoull/stoi` 未捕获异常 |
| 8 | `CMakeLists.txt` | 构建 | 低 | 未启用 `-Wall -Wextra` |
| 9 | `ChunkStore` | 已知限制 | 信息 | 全内存、非线程安全 |

## 5. 建议修改清单

1. 删除 `GearHash` 的 `updateGearHash` 与 `ChunkStore` 的 `isExist`/`lookup`
   （或标注为对外 API 并加注释）；确认 `vChunks` 的预留意图，若不需要则移除。
2. `GearHash.h` 的 `GEAR_TABLE` 改 `inline constexpr`。
3. `main.cpp` 的参数解析包裹 `try/catch`，非法输入给出用法提示而非崩溃。
4. `CMakeLists.txt` 增加 `-Wall -Wextra`（按编译器分支）。
5. `Hash.cpp` 增加 `EVP_MD_CTX` 的 RAII 释放与返回值检查。

以上均为非阻塞性改进，不改变算法行为与实验结论。
