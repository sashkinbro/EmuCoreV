cmake_minimum_required(VERSION 3.20)
project(emucorev_core_host_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Self-contained: only header-only pieces of the vendored core and the vendored googletest
# are compiled, never the emulator. Host toolchain: llvm-mingw (see run-core-host-tests.ps1).
get_filename_component(ECV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp/emucorev/ECV" ABSOLUTE)
set(CORE_ROOT "${ECV_ROOT}/ECV")
set(GTEST_ROOT "${ECV_ROOT}/external/googletest/googletest")

add_library(gtest_host STATIC
    "${GTEST_ROOT}/src/gtest-all.cc"
    "${GTEST_ROOT}/src/gtest_main.cc")
target_include_directories(gtest_host PUBLIC "${GTEST_ROOT}/include" PRIVATE "${GTEST_ROOT}")
find_package(Threads REQUIRED)
target_link_libraries(gtest_host PUBLIC Threads::Threads)

add_executable(core_host_tests object_store_tests.cpp video_snapshot_tests.cpp)
target_include_directories(core_host_tests PRIVATE
    "${CORE_ROOT}/kernel/include"
    "${CORE_ROOT}/modules/SceVideodec")
target_link_libraries(core_host_tests PRIVATE gtest_host)
target_compile_options(core_host_tests PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME core_host_tests COMMAND core_host_tests)
