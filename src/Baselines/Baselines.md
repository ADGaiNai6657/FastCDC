# Baselines 模块文档

FastCDC 论文 §5.1 的对比基线分块算法，用于与 FastCDC 做同口径的定量对比。

- 头文件：`Baselines.h`
- 实现：`Baselines.cpp`
- 上层调用：`src/main.cpp` 的 `findCuts`
- 相关文档：`docs/DESIGN.md` §3/§8、`docs/PAPER_CONFORMANCE.md`、`docs/CODE_REVIEW.md` §2.4

## 1. 设计目标

三种基线共用同一套外层框架,**只有切点判定不同**，保证与 FastCDC 同口径对比：

```
while (last < n) {
    if (n - last <= minSize) { cuts.push_back(n); break; }   // 尾部兜底
    searchEnd = min(last + maxSize, n)                        // MaxSize 强切
    cut = searchEnd                                           // 默认强切
    for (i = last + minSize; i < searchEnd; ++i) { ...判定... cut = i; break; }
    cuts.push_back(cut); last = cut;
}
```

每个函数把**绝对切点**（末项 `== data.size()`）写入 `cuts`；空输入返回空切点。

| 函数 | 算法 | 判定依据 | 掩码/窗口 |
| --- | --- | --- | --- |
| `fixedSizeCuts` | FSC / XC | 固定步长 | 无（每 `chunkSize` 切一刀） |
| `gearCuts` | 原始 Gear CDC | `(hash & mask) == 0` | 连续低位掩码，窗口 = 掩码位宽 |
| `rabinCuts` | Rabin CDC | `(fp & (D-1)) == r` | 48 字节滑窗多项式 |

## 2. 对外接口（Baselines.h）

```cpp
auto fixedSizeCuts(std::string_view data, std::size_t chunkSize,
                   std::vector<std::size_t>& cuts) -> void;

auto gearCuts(std::string_view data, std::size_t minSize, std::size_t maxSize,
              std::size_t expectedSize, std::vector<std::size_t>& cuts) -> void;

auto rabinCuts(std::string_view data, std::size_t minSize, std::size_t maxSize,
               std::size_t expectedSize, std::vector<std::size_t>& cuts) -> void;
```

`minSize` / `maxSize` 由调用方（`main.cpp:findCuts`）按论文 §5.1 传入
（`min = expected/4`、`max = expected*8`）。

## 3. `fixedSizeCuts`（Baselines.cpp:13-24）

固定块长分块（XC），论文用 `10*1024`。实现：从 `chunkSize` 起每 `chunkSize` 切一
刀，末尾补 `n`；空输入直接返回。

```cpp
for (std::size_t pos = chunkSize; pos < n; pos += chunkSize)
    cuts.push_back(pos);
cuts.push_back(n);
```

特点：与内容无关，插入/删除会使后续所有边界整体错位，去重率通常最低。

> 注意（`PAPER_CONFORMANCE.md` OBS-7）：`chunkSize == 0` 时 `pos += 0` 死循环，
> 调用方需保证 `--fsc-size > 0`（默认 10240）。

## 4. `gearCuts`（Baselines.cpp:26-62）——原始 Gear CDC

与 FastCDC 的差异：使用**连续低位掩码**（无零填充），滑动窗口等于掩码位宽。

```cpp
base = floor(log2(expectedSize));
mask = (1ULL << base) - 1;                 // 连续低位，无零填充
...
for (i = last + minSize; i < searchEnd; ++i) {
    hash = gearRoll(hash, bytes[i]);
    if ((hash & mask) == 0) { cut = i; break; }
}
```

- `gearRoll` 复用 `src/GearHash/`（`hash = (hash << 1) + G[b]`）。
- 掩码无零填充 → 判定窗口偏小 → 论文归因于其平均块长明显大于 FastCDC、去重率偏低。
- 与 FastCDC 一样每块重置 `hash`。

> 保真度小偏差（`PAPER_CONFORMANCE.md` OBS-6）：原版 Gear CDC 为连续滚动、判据
> 常为 `fp mod D == r`；本实现逐块重置并跳过 MinSize 区间。用于对比趋势足够。

## 5. `rabinCuts`（Baselines.cpp:64-117）——Rabin CDC

48 字节滑动窗口 + 经典 Rabin 多项式指纹，判据 `(fp & (D-1)) == r`。

### 5.1 参数

| 符号 | 取值 | 依据 |
| --- | --- | --- |
| `kWindow` | 48 | LBFS 滑动窗口字节数 |
| `kPoly` | `0x3DA3358B4DC173` | 固定奇多项式常数 |
| `D` | `expectedSize` | 论文 §2 Eq.1：D 即平均块长 |
| `r` | `min(0x78, expectedSize-1)` | LBFS 口径，须 `< D` |

**FIX-1**：除数 `D` 必须随期望块长缩放（`PAPER_CONFORMANCE.md` FIX-1）。此前硬编码
`D = 0x2000`（8KB），导致 `--expected-size 4K/16K` 仍以 8KB 为目标，RC 平均块长几乎
不随期望块长变化。现令 `D = expectedSize`，`kDivisorMask = expectedSize - 1`。
`expectedSize < 2` 时直接返回（掩码为 0 会恒命中）。

### 5.2 滚动递推

窗口内维护多项式指纹，滚动式：

```
fp = fp * p + b[i] - b[i-window] * p^window   (mod 2^64)
```

- 初始化：先滚入前 `min(window, searchEnd-last)` 个字节（`Baselines.cpp:100-105`）。
- 滚动：每步先加新字节、再减去最旧字节 `p^window`（`:106-113`）。
- `pPow = p^window` 由循环累乘得到（`:84-87`），与递推式一致。
- 判定条件附加 `i - last >= minSize`，保证边界不早于 MinSize（`:109`）。

`p^window` 的移除系数经论文 Eq.1/Eq.2 推导正确，勿改（`PAPER_CONFORMANCE.md` §4）。

## 6. 与 FastCDC 的对比口径

三种基线共用 `min 跳过 + max 强切` 外层，唯一差异是切点判定，因此：

- 相同 `min/max/expected` 下，块长分布与去重率的差异只反映**判定函数**本身。
- 论文 Table 3 的结论：FC 去重率 ≈ RC 且速度远快于 RC；GC 去重率最低、平均块长最大。
- 复现结果见 `paper/REPRODUCTION.md` §3、§7。

## 7. 参数速查（默认 8KB）

| 参数 | 默认 | 说明 |
| --- | ---: | --- |
| `chunkSize`（FSC） | 10240 | `--fsc-size` |
| `minSize` | 2048 | `expected/4`，由 `main` 传入 |
| `maxSize` | 65536 | `expected*8`，由 `main` 传入 |
| `D`（Rabin） | `expectedSize` | FIX-1 |
| `r`（Rabin） | 0x78 | `< D` |

## 8. 已知问题与注意

- **`fixedSizeCuts` 零块长死循环**：`chunkSize == 0` 时 `pos += 0`；调用方需保证 > 0。
- **Gear 保真度偏差**：逐块重置哈希、跳过区不计入（OBS-6）；原始 Gear 为连续滚动。
- **AE 基线未实现**：论文 Table 3 提及 AE-based CDC，本项目未做（`paper/REPRODUCTION.md` §10）。
- **Rabin 初值**：窗口初始化从 `last` 起滚 `window` 字节，未跨块携带历史；块首
  `minSize` 内不判定，属同口径设计选择。
