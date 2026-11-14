cmake_minimum_required(VERSION 3.10)

project(FAT16)

option(BUILD_EXAMPLES "Build the examples project as well" OFF)

add_library(FAT16
    include/fat16/fat16.h
    src/fat16.cpp)

target_include_directories(FAT16 PUBLIC include)

if (BUILD_EXAMPLES)
add_executable(FAT16_EXTRACT
    examples/extract.cpp)

target_link_libraries(FAT16_EXTRACT PRIVATE FAT16)
endif()
