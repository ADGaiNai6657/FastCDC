//
// Baselines.h
// FastCDC 论文 5.1 的对比基线：
//   - Fixed-Size Chunking (FSC / XC)，固定 10KB；
//   - Gear-based CDC（原始 Gear，无零填充，滑动窗口 = 掩码位宽）；
//   - Rabin-based CDC（48 字节窗口，D=0x2000、r=0x78，LBFS 配置）。
// 每个函数把绝对切点（末项 == data.size()）写入 cuts。
//

#ifndef FASTCDC_BASELINES_H
#define FASTCDC_BASELINES_H

#include <cstddef>
#include <string_view>
#include <vector>

// 固定块长分块（XC）。chunkSize 即固定块大小（论文用 10*1024）。
auto fixedSizeCuts(std::string_view data,
                   std::size_t chunkSize,
                   std::vector<std::size_t>& cuts) -> void;

// 原始 Gear-based CDC：掩码为连续低位（无零填充），从 MinSize 起滚动。
// expectedSize 决定掩码位数（base = log2(expectedSize)）。
auto gearCuts(std::string_view data,
              std::size_t minSize,
              std::size_t maxSize,
              std::size_t expectedSize,
              std::vector<std::size_t>& cuts) -> void;

// Rabin-based CDC：48 字节滑动窗口，fp mod D == r，其中 D = expectedSize
// （论文 §2 Eq.1：D 即平均块长；§4.2 的 D=0x2000 对应 8KB 期望块长）。
auto rabinCuts(std::string_view data,
               std::size_t minSize,
               std::size_t maxSize,
               std::size_t expectedSize,
               std::vector<std::size_t>& cuts) -> void;

#endif //FASTCDC_BASELINES_H
