#include <iostream>
#include <string>
#include <GearHash/GearHash.h>

#include "GearHash/GearHash.h"

struct Object {
    std::string str = "NULL";
    bool init = false;
    uint64_t hash = 0;
};

int main() {

    Object object {
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ", false, 0
    };
    // uint64_t hash = 0;

    for (size_t i = 0; i < object.str.size(); ++i) {
        uint8_t temp = static_cast<uint8_t>(object.str[i]);
        getHashValue(&temp, sizeof(temp), object.hash, object.init);
    }

    std::cout << object.hash <<std::endl;

    object.str.append("X");

    // getHashValue(static_cast<uint8_t>(object.str.back()), 1, object.hash, object.init);
    return 0;
}
