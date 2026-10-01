cmake_minimum_required(VERSION 4.1)
project(FastCDC)

set(CMAKE_CXX_STANDARD 20)

add_executable(FastCDC src/main.cpp
        src/GearHash/GearHash.cpp
        src/GearHash/GearHash.h
        src/BreakingApart/BreakingApart.cpp
        src/BreakingApart/BreakingApart.h
        src/ChunkStore/ChunkStore.cpp
        src/ChunkStore/ChunkStore.h)

target_include_directories(FastCDC PRIVATE src)
