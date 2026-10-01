//
// Created by user on 2026/9/30.
//

#include "GearHash.h"

//*data指针指向窗口内第一个数据；length代表视窗长度；hash就是hash，没啥好说的；init代表是否初始化过
auto getHashValue(const std::uint8_t *data, size_t length, uint64_t &hash, bool &init) -> uint64_t {

    if (!init) {    //没初始化？说明这是一段新的数据，需要从头开始填充！

        for (auto i = 0; i < length; i++) {
            hash = (hash << 1) + GEAR_TABLE[data[i]];
        }

        init = true;
        return hash;
    } else {            //初始化过了？说明之前已经填充过了，只需要加入新的数据就行

        hash = (hash << 1) + GEAR_TABLE[data[length - 1]];
        return hash;
    }
};

