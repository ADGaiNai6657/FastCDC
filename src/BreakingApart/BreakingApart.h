//
// Created by ADGaiNai6657 on 2026/9/30.
//
#pragma once
#include <cstddef>

#ifndef FASTCDC_BREAKINGAPART_H
#define FASTCDC_BREAKINGAPART_H

struct ChunkerParams {
    std::size_t Mask;
    std::size_t window;
    std::size_t MinSize;
    std::size_t NormalSize;
    std::size_t MaxSize;
};

struct BreakingApartConfig {
    ChunkerParams big;
    ChunkerParams middle;
    ChunkerParams small;
};

#endif //FASTCDC_BREAKINGAPART_H
