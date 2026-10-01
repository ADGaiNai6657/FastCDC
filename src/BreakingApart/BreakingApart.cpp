//
// Created by ADGaiNai6657 on 2026/9/30.
//
#pragma once

#include "BreakingApart.h"
#include "GearHash/GearHash.h"

constexpr std::size_t WINDOW = 48;
constexpr std::size_t TARGET_CHUNK_SIZE = 8096;
constexpr std::size_t MINIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE * 0.5;
constexpr std::size_t MAXIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE * 8;
constexpr ull MASK_S = 0x0003590703530000LL; //15 of ones; Harder to hit,should be put in the big para. WITH NC
constexpr ull MASK_M = 0x0000d90303530000LL; //13 of ones; Middium, put in middle. WITHOUT NC
constexpr ull MASK_L = 0x0000d90003530000LL; //11 of ones; Easier to hit, put in small para. WITH NC

//initialize the para. list.
namespace {
    // BreakingApartConfig BEFORE {
    //     WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MaskS
    // };
    //
    // BreakingApartConfig WITHOUT_NC {
    //     WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE,MaskM
    // };
    //
    // BreakingApartConfig AFTER {
    //     WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MaskL
    // };
    BreakingApartConfig breakingApartConfig{
        WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MASK_S, MASK_M, MASK_L
    };
}

auto BoundariesFinder (const std::string_view data,
                       std::vector<std::vector<size_t>> &vPosition,
                       // const BreakingApartConfig &parameter,
                       const bool &is_NC)
-> void {

    //太短了直接抛错误
    if (data.size() < breakingApartConfig.window) {
        throw std::length_error ("TOO SHORT!");
    };

    //将此二维向量向后添加一个空向量准备加入cut-point的下标信息
    vPosition.emplace_back();

    const std::uint8_t* PTR_FIRST_DATA = reinterpret_cast<const std::uint8_t*>(data.data());    //开头哨兵指针，指向data的开头
    const uint8_t* lastPosition = reinterpret_cast<const std::uint8_t*>(data.data());    //指向上一次的cut-point;初始化为指向第一个字节；
    const uint8_t* ptrData = reinterpret_cast<const uint8_t*>(data.data()); //用uint8_t类型指针指向string_view所指向的内存，指向window的开头位置
    const uint8_t* ptrEnd = ptrData + data.size();                          //末尾哨兵指针，指向data末尾
    bool is_init = false;
    std::uint64_t hash = 0;

    while (ptrData < ptrEnd - breakingApartConfig.window) {
        //二指针相差小于MinSize？属于SkipJudgement阶段，接着向后跳
        if (std::ptrdiff_t(ptrData - lastPosition) < breakingApartConfig.MinSize) {
            continue;
        }

        //使用NC,且在TARGET_CHUNK_SIZE之前？使用BEFORE配置中的Mask以降低命中概率！
        if (is_NC == true && std::ptrdiff_t(ptrData-lastPosition) < breakingApartConfig.NormalSize) {
            getHashValue(ptrData, breakingApartConfig.window, hash, is_init);

            //与Mask按位与。满足条件？push_back()，并，更新lastPosition指针。 通过 当前位置指针 - 首字节指针 获取到下标。
            if ((hash & breakingApartConfig.MaskS) == 0) {
                vPosition.back().push_back(std::ptrdiff_t(ptrData - PTR_FIRST_DATA));
                lastPosition = ptrData;
                continue;
            }
        }

        if (is_NC == true && std::ptrdiff_t(ptrData - lastPosition) > breakingApartConfig.NormalSize) {
            getHashValue(ptrData, breakingApartConfig.window, hash, is_init);

            if ((hash & breakingApartConfig.MaskL) == 0) {
                vPosition.back().push_back(std::ptrdiff_t(ptrData - PTR_FIRST_DATA));
                lastPosition = ptrData;
                continue;
            }
        }

        if (is_NC == false) {
            getHashValue(lastPosition, breakingApartConfig.window, hash, is_init);

            if (hash & breakingApartConfig.MaskM == 0) {
                vPosition.back().push_back(std::ptrdiff_t(ptrData - lastPosition));
                lastPosition = ptrData;
                continue;
            }
        }
    }
}
