//
// Created by ADGaiNai6657 on 2026/9/30.
//
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string_view>

#ifndef FASTCDC_BREAKINGAPART_H
#define FASTCDC_BREAKINGAPART_H

using ull = unsigned long long;

// struct ChunkerParams {
//     std::size_t Mask;
// };

struct BreakingApartConfig {
    std::size_t window;
    std::size_t MinSize;
    std::size_t NormalSize;
    std::size_t MaxSize;
    std::size_t MaskS;
    std::size_t MaskM;
    std::size_t MaskL;
    // ChunkerParams big;
    // ChunkerParams middle;
    // ChunkerParams small;
};


auto BoundariesFinder (const std::string_view data,
                       std::vector<std::vector<size_t>> &vPosition,
                       // const BreakingApartConfig &parameter,
                       const bool &is_NC)
-> void ;

#endif //FASTCDC_BREAKINGAPART_H
