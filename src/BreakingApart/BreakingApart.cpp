//
// Created by ADGaiNai6657 on 2026/9/30.
//

#include "BreakingApart.h"

const std::size_t WINDOW = 48;
const std::size_t TARGET_CHUNK_SIZE = 8096;
const std::size_t MINIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE * 0.5;
const std::size_t MAXIMUM_CHUNK_SIZE = TARGET_CHUNK_SIZE * 8;
const ull MaskS = 0x0003590703530000LL; //15 of ones; Harder to hit,should be put in the big para. WITH NC
const ull MaskM = 0x0000d90303530000LL; //13 of ones; Middium, put in middle. WITHOUT NC
const ull MaskL = 0x0000d90003530000LL; //11 of ones; Easier to hit, put in small para. WITH NC

//initialize the para. list.
namespace {
    BreakingApartConfig BEFORE {
        WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MaskS
    };

    BreakingApartConfig WITHOUT_NC {
        WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE,MaskM
    };

    BreakingApartConfig AFTER {
        WINDOW, MINIMUM_CHUNK_SIZE, TARGET_CHUNK_SIZE, MAXIMUM_CHUNK_SIZE, MaskL
    };
}

auto BoundariesFinder (const std::string_view data,
                       std::vector<std::vector<size_t>> &vPosition,
                       const BreakingApartConfig &parameter)
-> void {

    //太短了直接抛错误
    if (data.size() < parameter.window) {
        throw std::length_error ("TOO SHORT!");
    };

    //将此二维向量向后添加一个空向量准备加入cut-point的下标信息
    vPosition.emplace_back();

    const std::uint8_t* PTR_FIRST_DATA = reinterpret_cast<const std::uint8_t*>(data.data());    //开头哨兵指针，指向data的开头
    uint8_t* lastPosition = reinterpret_cast<std::uint8_t*>(const_cast<char*>(data.data()));    //指向上一次的cut-point;初始化为指向第一个字节；CAUTION！此指针仅用作指向上次的cut-point！不要使用这个指针进行任何内容修改！
    const uint8_t* ptrData = reinterpret_cast<const uint8_t*>(data.data()); //用uint8_t类型指针指向string_view所指向的内存，指向window的开头位置
    const uint8_t* ptrEnd = ptrData + data.size();                          //末尾哨兵指针，指向data末尾

    while (ptrData < ptrEnd - parameter.window) {
        if (std::ptrdiff_t(ptrData-lastPosition) < parameter.window) {
            continue;
        }

        if ()
    }
}
