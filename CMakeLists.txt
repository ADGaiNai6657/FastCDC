cmake_minimum_required(VERSION 4.1)
project(FastCDC)

set(CMAKE_CXX_STANDARD 20)

# 复现论文速度实验：默认 Release(-O3)。可在命令行用 -DCMAKE_BUILD_TYPE=... 覆盖。
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Homebrew 的 openssl 是 keg-only，未必在默认搜索路径；显式给出候选根目录。
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(_openssl_candidate /opt/homebrew/opt/openssl /usr/local/opt/openssl)
        if(EXISTS "${_openssl_candidate}/include/openssl/evp.h")
            set(OPENSSL_ROOT_DIR "${_openssl_candidate}" CACHE PATH "OpenSSL install prefix")
        endif()
    endforeach()
endif()

find_package(OpenSSL REQUIRED)

add_executable(FastCDC
        src/main.cpp
        src/GearHash/GearHash.cpp
        src/GearHash/GearHash.h
        src/Hash/Hash.cpp
        src/Hash/Hash.h
        src/BreakingApart/BreakingApart.cpp
        src/BreakingApart/BreakingApart.h
        src/Baselines/Baselines.cpp
        src/Baselines/Baselines.h
        src/ChunkStore/ChunkStore.cpp
        src/ChunkStore/ChunkStore.h)

target_include_directories(FastCDC PRIVATE src)
target_link_libraries(FastCDC PRIVATE OpenSSL::Crypto)
