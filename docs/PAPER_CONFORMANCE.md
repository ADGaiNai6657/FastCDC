# FastCDC 论文一致性审查（面向 Agent）

本文件对照 `paper/atc16-paper-xia.pdf`（USENIX ATC'16）审查实现的**算法保真度**，
与 `docs/CODE_REVIEW.md`（通用代码卫生）互补。审查基线为论文的
Algorithm 1（p.108）、§4.2–4.5、§5.1–5.5。

> Agent 使用说明：本文件是**可执行任务清单**。**必须修复第 1、2 项**（论文一致性
> 缺陷，会改变复现数值）；其余项为观察记录，本轮不强制。完成后按 §3 验收，
> 并同步更新 `paper/REPRODUCTION.md` 的受影响表格。

## 1. 必须修复

### FIX-1 Rabin 基线除数未随期望块长缩放（高）

**现状**

- `src/Baselines/Baselines.cpp:72-75` 把 Rabin 除数/阈值硬编码为 8KB：
  `kDivisorMask = 0x1FFF`（D = 0x2000）、`kThreshold = 0x78`。
- `rabinCuts(...)`（`Baselines.cpp:64-67`）签名**没有** `expectedSize` 参数，
  `src/Baselines/Baselines.h:31-34` 同样。
- `src/main.cpp:74-79` 调用时只传 `min/max`，不传 `expectedSize`。

**论文依据**

- §2 Eq.1：Rabin 的多项式「D 是平均块长」。
- §4.2：`D=0x2000, r=0x78` 是**为得到 8KB 期望块长**而设的取值。
- §5.1：Rabin 基线的 max/min = 8×/¼× **期望块长**，故 D 也应随期望块长变化。

**影响**

`--algo rabin --expected-size 4096|16384` 仍以 8KB 为目标块长，导致
`paper/REPRODUCTION.md` Table 3 的 RC 列、Fig 10/13 在 4K/16K 上不可比。
佐证：该报告 RC 平均块长几乎不随期望块长变化（LNX 7658/8594/10131），
而论文为缩放关系（TAR 5561/11873/24067）。

**修改点**

1. `src/Baselines/Baselines.h:31-34`：`rabinCuts` 增加 `std::size_t expectedSize` 形参
   （放在 `maxSize` 之后、`cuts` 之前，保持风格一致）。
2. `src/Baselines/Baselines.cpp:64-75`：
   - 形参同步增加 `expectedSize`；
   - 除数改为 `D = expectedSize`，即 `kDivisorMask = expectedSize - 1`；
   - 阈值保留 `0x78`（LBFS 口径），但需保证 `< D`（`expectedSize` 过小时取
     `min(0x78, expectedSize-1)` 或直接 0）；
   - 对 `expectedSize < 2` 做防御（`expectedSize` 为 0 会使掩码为 0，恒命中）。
   - 48 字节滑窗 `kWindow` 不变。
3. `src/main.cpp:74-79`：调用处补传 `opts.expectedSize`。

**参考实现**

```cpp
auto rabinCuts(std::string_view data,
               std::size_t minSize,
               std::size_t maxSize,
               std::size_t expectedSize,
               std::vector<std::size_t>& cuts) -> void {
    if (expectedSize < 2) return;      // 防御：过小无意义
    const std::uint64_t kDivisorMask = expectedSize - 1; // D = expectedSize
    const std::uint64_t kThreshold = 0x78u;
    // ...其余不变，判据仍为 (fp & kDivisorMask) == kThreshold
}
```

### FIX-2 NC 归一化切换点（NormalSize）随 MinSize 调整（高）

**现状**

- `src/BreakingApart/BreakingApart.cpp:68`：`makeFastCDCConfig` 固定
  `NormalSize = expectedSize`。
- `src/BreakingApart/BreakingApart.cpp:47-52`：`maskFor` 以
  `chunkOffset < NormalSize` 决定用 `MaskS` 还是 `MaskL`。

**论文依据**

- §4.4：切换发生在期望块长处（8KB 示例）。
- §5.4：带 NC 的实验里「归一化块长配置为 **4KB + 最小块长**」，
  即 `Min4KB → 8KB`、`Min8KB → 12KB`。

**影响（退化为实缺陷）**

当 `MinSize == NormalSize`（即 `--min-size 8192 --nc-level 1|2|3`）时，第一段循环
从 `i = last + MinSize` 起，`chunkOffset` 恒 `>= NormalSize`，**`MaskS` 永不参与**，
NC 退化为单一 `MaskL`。受影响的用例：
`tools/run_experiments.sh:53-54`（Fig 12 的 Min8K）、`:68`（Table 4/5 的
FC-NC-Min8KB）。这解释了复现报告 FC-NC-Min8K 平均块长（TAR 10420）远低于
论文（14076）。

**修改点**

- `src/BreakingApart/BreakingApart.cpp:68`：NC 开启时令
  `NormalSize = std::max(expectedSize, minSize + expectedSize / 2)`；
  NC 关闭（`level == 0`）时仍取 `expectedSize`（不影响非 NC 路径）。

**选择该公式的理由（务必保留注释）**

- `minSize = expected/4`（默认 2KB）：`minSize + expected/2 = 6KB < 8KB`
  → 取 `max` 得 8KB，与 Algorithm 1 的 `NormalSize = 8KB` 一致。
- `Min4KB`：`4KB + 4KB = 8KB` → 与 §5.4（4KB+Min）一致。
- `Min8KB`：`8KB + 4KB = 12KB` → 与 §5.4 一致，且保证 `NormalSize > MinSize`，
  `MaskS` 有作用区间，消除退化。

**注意**：论文 §5.4 与 Algorithm 1 对 `Min2KB` 的表述存在张力（见 §5 待澄清），
上述 `max` 形式是同时满足两者的最小改动；`Min4KB/8KB`（实验脚本实际使用的
NC 配置）结果确定。

## 2. 其余观察（本轮不强制）

| # | 位置 | 说明 |
|---|------|------|
| OBS-3 | `BreakingApart.cpp:24-44` | level 1/3 与 4K/16K 掩码为自生成（`[16,49]` 等距置位），非论文公开经验值。`REPRODUCTION.md §9` 已声明；注释「按同样的零填充分布」仅为近似，应弱化措辞。 |
| OBS-4 | `BreakingApart.cpp:99-108`、`Baselines.cpp:52-58`、`:103` | `MinSize==0` 且 `i==last` 命中时 `cut==last`，`last` 不前进 → 死循环。已核验当前 `GEAR_TABLE` 与所用掩码下不触发（潜伏）。建议加 `cut > last` 守卫。 |
| OBS-5 | `tools/make_synthetic_backups.py:56-65` | SYN 仅等长原位替换，无边界偏移，对 XC/固定分块有利，削弱 Table 4/5 的 XC 对比意义。`REPRODUCTION.md §6` 已声明。 |
| OBS-6 | `Baselines.cpp:50-58` | Gear 基线逐块重置哈希、跳过区不计入；原版 Gear CDC 为连续滚动，且判据通常为 `fp mod D == r`。保真度小偏差。 |
| OBS-7 | `Baselines.cpp:20` | `fixedSizeCuts` 在 `chunkSize==0` 时死循环；`main.cpp` 的 `stoull/stoi` 未捕获异常；`CMakeLists.txt` 未开 `-Wall -Wextra`；`updateGearHash`/`isExist`/`lookup`/`vChunks` 冗余。详见 `docs/CODE_REVIEW.md`。 |

## 3. 验收标准

修复 FIX-1、FIX-2 后，必须重建并以以下命令自检（Release）：

```bash
cmake -B cmake-build-release -DCMAKE_BUILD_TYPE=Release && cmake --build cmake-build-release

# FIX-1：RC 平均块长应随期望块长近似缩放（此前几乎不变）
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo rabin --expected-size 4096
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo rabin --expected-size 8192
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo rabin --expected-size 16384

# FIX-2：Min8K 的 NC 不再退化，平均块长应明显大于旧实现、且 NC2 与 NC0 有区分
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo fastcdc --expected-size 8192 --min-size 8192 --nc-level 0
./cmake-build-release/FastCDC Dataset/DataSet_3 --algo fastcdc --expected-size 8192 --min-size 8192 --nc-level 2
```

- FIX-1：TAR 的 RC `avgChunk` 在 4K/8K/16K 上递增（趋势同论文 Table 3）。
- FIX-2：`Min8K NC2` 的 `avgChunk` 相比修复前上升（向论文 Table 5 的 ~14KB 靠拢），
  且 `NC2 != NC0`；`Min4K NC2` 数值应基本不变。
- 非 NC 默认用例（`--no-nc`、min=2KB）结果**不应改变**（回归基线）。
- 重跑 `tools/run_experiments.sh`，更新 `paper/REPRODUCTION.md` 中受影响的
  Table 3（RC 列）、Fig 12（Min8K）、Table 4/5（FC-NC-Min8KB），并在 §9 记录本次修正。

## 4. 保持不变（已核对与论文一致，勿改动）

- `MASK_A_13/MASK_S_15/MASK_L_11` 常量与 Algorithm 1 完全一致（popcount 13/15/11，
  最低位 16 → 滑窗 ≈48B）。
- NC 等级 → 位数映射 `(base+L, base-L)` 对应 `(14,12)/(15,11)/(16,10)`，正确。
- Gear 公式 `(h<<1)+G[b]`、`min=expected/4`、`max=expected*8`、去重率定义
  `1-uniq/total`、SHA-1 去重指纹，均一致。
- Rabin 滚动递推的移除系数为 `p^48`（= `p^α`），经 Eq.1/Eq.2 推导**正确**，勿改。

## 5. 待澄清（记录，不阻塞）

- §5.4「归一化块长 = 4KB + 最小块长」与 Algorithm 1 示例 `NormalSize=8KB, MinSize=2KB`
  （按 §5.4 应为 6KB）存在表述张力。本次采用 `max(expected, Min+expected/2)` 同时满足
  两者；若后续获得论文源码，应据实校正。
