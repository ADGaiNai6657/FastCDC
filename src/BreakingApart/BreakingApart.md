# BreakingApart 模块文档

FastCDC 边界查找模块，实现论文 *FastCDC: a Fast and Efficient Content-Defined
Chunking Approach for Data Deduplication*（USENIX ATC'16）Algorithm 1 的三项技术。
对外仅两个入口：`makeFastCDCConfig`（参数生成）与 `BoundariesFinder`（扫描找切点）。

- 头文件：`BreakingApart.h`
- 实现：`BreakingApart.cpp`
- 上层调用：`src/main.cpp` 的 `findCuts` / `processBuffer`
- 相关文档：`docs/DESIGN.md` §4、`docs/PAPER_CONFORMANCE.md`、`docs/CODE_REVIEW.md` §2.3

## 1. 设计目标

CDC 要求切点**只由内容决定、与绝对位置无关**。本模块把问题拆成两半：

| 关注点 | 入口 | 职责 |
| --- | --- | --- |
| 参数生成 | `makeFastCDCConfig` | 把「期望块长 / 最小块长 / NC 等级」翻译成掩码与三个尺寸 |
| 扫描找切点 | `BoundariesFinder` | 用固定骨架在字节流上滚动、判定、产出绝对切点 |

拆分的目的：扫描热循环内不做除法、对数或掩码构造，全部预计算进
`BreakingApartConfig`；同一条扫描逻辑适配任意参数组合。

## 2. 对外接口（BreakingApart.h）

### 2.1 `BreakingApartConfig`（`BreakingApart.h:17-25`）

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `MinSize` | `size_t` | 最小块长；切点判定从这个偏移之后才开始 |
| `NormalSize` | `size_t` | NC 归一化切换点（块内相对偏移） |
| `MaxSize` | `size_t` | 最大块长，窗口内始终未命中则强制切分 |
| `MaskA` | `uint64_t` | 非 NC 掩码（base 个 1 位，论文 Algorithm 1 的 MaskA） |
| `MaskS` | `uint64_t` | NC 在 `NormalSize` 之前使用的较难命中掩码（base+L 位） |
| `MaskL` | `uint64_t` | NC 在 `NormalSize` 之后使用的较易命中掩码（base-L 位） |
| `NcLevel` | `int` | 0=非 NC；1/2/3=归一化等级 |

用结构体而非硬编码常量，是为了让命令行参数直接映射为一份 `cfg`，扫描器不关心
这些值的来源（见 `docs/DESIGN.md` §4）。

### 2.2 `makeFastCDCConfig(expectedSize, minSize, ncLevel)`

依据期望块长 / 最小块长 / NC 等级构造配置。8KB 的 11/13/15 位掩码使用论文公开
常量；其余位宽按同样的零填充分布生成（复现假设）。

### 2.3 `BoundariesFinder(data, vPosition, config)`

对一段缓冲查找切点，把结果**追加**为 `vPosition` 的末尾一组（绝对字节偏移，
末项 `== data.size()`）。`main.cpp` 每次新建 `positions` 后取 `.back()`。

## 3. 参数生成 `makeFastCDCConfig`（BreakingApart.cpp:56-79）

### 3.1 掩码位宽 `base`（:60-63）

```
base = floor(log2(expectedSize))
```

掩码里放 `base` 个 1 位。若 Gear 哈希均匀，每个位置命中 `(hash & Mask) == 0`
的概率为 `2^-base`，因此**期望间隔 ≈ 2^base ≈ expectedSize**——这是「期望块长」
的唯一来源。用 `floor(log2)` 是保守选择：非 2 的幂向下取位，平均块长略短。

### 3.2 三个掩码（:65-67）

| 掩码 | 位数 | 命中概率 | 用途 |
| --- | --- | --- | --- |
| `MaskA` | `base` | `2^-base` | 非 NC（level 0） |
| `MaskS` | `base + level` | 更低（更难命中） | NC 前段（`< NormalSize`） |
| `MaskL` | `base - level` | 更高（更易命中） | NC 后段（`>= NormalSize`） |

一难一易，把块长分布**向 `NormalSize` 挤压**，即归一化分块 NC（论文 §4.4）。
level=0 时三者相等，退化为普通 FastCDC。

> level > 1 时 `base - level` 可能为负，代码用 `std::max(1, base - level)` 保护。

### 3.3 归一化切换点 `NormalSize`（:75-77）

```
level > 0 : NormalSize = max(expectedSize, minSize + expectedSize/2)
level == 0: NormalSize = expectedSize
```

**为何不能取固定 `expectedSize`**：`maskFor` 用块内相对偏移 `i-last < NormalSize`
选掩码；而扫描从 `last + MinSize` 才开始。若 `NormalSize <= MinSize`，则
`i-last` 恒 `>= MinSize >= NormalSize`，`MaskS` 永不参与，NC 退化为单一 `MaskL`。
这正是 `--min-size 8192` 时旧实现 `NormalSize=8192` 的缺陷（`PAPER_CONFORMANCE.md`
FIX-2）。

**为何是 `min + expected/2`**：论文 §5.4「归一化块长 = 4KB + 最小块长」，而
`expected/2 = 4KB`。但 Algorithm 1 的 8KB 示例 Min2K 给出 `NormalSize = 8KB`，
按 §5.4 应为 `2K + 4K = 6KB`，二者存在表述张力。取 `max` 同时满足：

| MinSize | `min + expected/2` | `max(expected, ...)` | 依据 |
| ---: | ---: | ---: | --- |
| 2KB（默认） | 6KB | 8KB | 与 Algorithm 1 一致 |
| 4KB | 8KB | 8KB | 与 §5.4 一致 |
| 8KB | 12KB | 12KB | 与 §5.4 一致，且 `> MinSize` |

NC 关闭时仍取 `expectedSize`，不影响非 NC 路径。

### 3.4 最大块长 `MaxSize`（:78）

`MaxSize = expectedSize * 8`，论文给定的强切上界，保证单块不会无限大。

## 4. 掩码构造 `buildMask`（BreakingApart.cpp:24-44）

FastCDC 最反直觉的设计，对应论文 §4.2「优化哈希判定」。

- Gear 滚动式 `hash = (hash << 1) + G[b]`：每读一个新字节，旧字节贡献左移一位。
  故哈希**低位只依赖最近几个字节**，**高位才依赖更久之前的内容**。
- 若掩码用连续低位 `(1<<base)-1`（原始 Gear 的做法），命中的窗口太短、质量差。
- **把低位清零、把 1 位散布到 `[16, 49]`**：判定实际覆盖至少 16 字节窗口，同时
  高位受更早字节影响，等于免费获得更长的滑动窗口。这是零填充掩码的价值。
  - 下界 16：保证窗口 ≥ 16 字节。
  - 上界 49：再高会逼近 64 位顶部，`(hash << 1)` 溢出、信息被挤出。
  - 等距置位：使掩码均匀、不扎堆。
- 11/13/15 位直接返回论文常量 `MASK_L_11 / MASK_A_13 / MASK_S_15`（`BreakingApart.cpp:18-20`），
  它们的最低置位也正是 bit 16。其他位宽（level 1/3、4K/16K）论文未公开，按同一
  「低位留零 + 等距」规则生成，属复现假设。

## 5. 扫描算法 `BoundariesFinder`（BreakingApart.cpp:81-120）

```cpp
vPosition.emplace_back();          // 追加一组切点，取引用 cuts
if (n == 0) return;

const bool isNC = config.NcLevel > 0;
std::size_t last = 0;
while (last < n) {
    if (n - last <= config.MinSize) { cuts.push_back(n); break; }   // (1) 尾部兜底
    const std::size_t searchEnd = std::min(last + config.MaxSize, n); // (2) 窗口上界
    std::uint64_t hash = 0;
    std::size_t cut = searchEnd;                                      // (3) 默认强切
    for (std::size_t i = last + config.MinSize; i < searchEnd; ++i) { // (4) 跳过 MinSize
        hash = gearRoll(hash, bytes[i]);
        if ((hash & maskFor(config, i - last, isNC)) == 0) {          // (5) 命中判定
            cut = i;
            break;
        }
    }
    cuts.push_back(cut);
    last = cut;
}
```

| 标记 | 作用 | 为什么 |
| --- | --- | --- |
| (1) | 剩余 `<= MinSize` 直接切到 `n` | 尾部不足以再成一块，整段作最后一块；`<=` 使恰好 MinSize 的余量也并入 |
| (2) | `searchEnd = min(last+MaxSize, n)` | MaxSize 强切上界 + 文件尾 clamp |
| (3) | `cut = searchEnd` | 窗口内始终未命中掩码时的 fallback（到最大块长强制切） |
| (4) | 从 `last + MinSize` 起滚 | 次最小块切点跳过（论文 §4.3）：省去跳过区间的哈希计算以提速 |
| (5) | 每滚一字节立即判命中 | `maskFor` 按块内相对偏移选 MaskS/MaskL |

**关键取舍**：`hash` 每块重置为 0，跳过区间 `[last, last+MinSize)` 的字节完全不
参与指纹。这是论文的 jumping 优化，块边界只由切点后的局部窗口决定，利于快速收敛。

### 5.1 `maskFor`（BreakingApart.cpp:47-52）

```cpp
if (!isNC) return cfg.MaskA;
return (chunkOffset < cfg.NormalSize) ? cfg.MaskS : cfg.MaskL;
```

通过 `chunkOffset = i - last`（块内相对偏移）决定用 `MaskS` 还是 `MaskL`。必须用
相对偏移：NC 的「前段/后段」是相对当前块起点而言，用绝对位置会让切换点漂移、NC 失效。

## 6. 调用关系与数据流

```
main.cpp: findCuts
  cfg = makeFastCDCConfig(expected, min, ncLevel)   // 预计算
  BoundariesFinder(data, positions, cfg)            // 扫描
    positions.back() == cuts（绝对偏移，末项 == data.size()）
  → emitChunk(data, prev, cut)                      // 发射到 ChunkStore
```

分块本身与存储无关：`BoundariesFinder` 只读 `data`、只写切点。

## 7. 参数速查（默认 8KB）

| 参数 | 默认 | 推导 |
| --- | ---: | --- |
| `base` | 13 | `floor(log2(8192))` |
| `MinSize` | 2048 | `expected/4` |
| `MaxSize` | 65536 | `8 × expected` |
| `NormalSize`（NC） | `max(expected, min+expected/2)` | FIX-2 |
| `MaskA` | `0x0000d90303530000` | 13 位，论文常量 |
| `MaskS`（L2） | `0x0003590703530000` | 15 位，论文常量 |
| `MaskL`（L2） | `0x0000d90003530000` | 11 位，论文常量 |

非 NC 平均块长 ≈ `MinSize + expectedSize`；NC 更靠近 `NormalSize`。

## 8. 已知问题与注意

- **接口隐晦**：结果经 `vPosition.back()` 传回，可考虑直接返回 `vector<size_t>`。
- **潜在死循环**：`MinSize == 0` 且 `i == last` 命中掩码时 `cut == last`，`last` 不
  前进。当前 `GEAR_TABLE` 与所用掩码下不触发（`PAPER_CONFORMANCE.md` OBS-4），
  建议加 `cut > last` 守卫。
- **level 1/3 与部分位宽掩码为自造**：论文仅公开 8KB level 2 的 11/13/15 常量，
  其余为复现假设（`paper/REPRODUCTION.md` §9）。
- **非 2 的幂期望值**：`floor(log2)` 向下取较小位宽，属设计选择而非缺陷。
