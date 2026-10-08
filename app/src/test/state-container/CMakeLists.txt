cmake_minimum_required(VERSION 3.22)
project(EmuCoreVStateContainerTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp" ABSOLUTE)
set(EXTERNAL "${NATIVE}/vita3k/external")
file(GLOB BOOST_FILESYSTEM_SOURCES "${EXTERNAL}/boost/libs/filesystem/src/*.cpp")
add_executable(state-container-tests
    archive_tests.cpp
    memory_image_tests.cpp
    ngs_state_io_tests.cpp
    kernel_base_io_tests.cpp
    kernel_module_validation_tests.cpp
    render_snapshot_gate_tests.cpp
    save_state_operation_tests.cpp
    save_state_operation_peer.cpp
    "${NATIVE}/emucorev/src/savestate/archive.cpp"
    "${NATIVE}/emucorev/src/savestate/memory_image.cpp"
    "${NATIVE}/emucorev/src/savestate/display_state_io.cpp"
    "${NATIVE}/emucorev/src/savestate/ngs_state_io.cpp"
    "${NATIVE}/emucorev/src/savestate/kernel_base_io.cpp"
    "${NATIVE}/emucorev/src/savestate/kernel_module_validation.cpp"
    "${EXTERNAL}/miniz/miniz.c"
    "${EXTERNAL}/googletest/googletest/src/gtest-all.cc"
    "${EXTERNAL}/googletest/googletest/src/gtest_main.cc"
    ${BOOST_FILESYSTEM_SOURCES})
target_include_directories(state-container-tests PRIVATE
    "${NATIVE}/emucorev/include" "${NATIVE}/vita3k/vita3k/util/include"
    "${NATIVE}/vita3k/vita3k/ngs/include" "${NATIVE}/vita3k/vita3k/mem/include"
    "${NATIVE}/vita3k/vita3k/kernel/include" "${NATIVE}/vita3k/vita3k/cpu/include"
    "${NATIVE}/vita3k/vita3k/emuenv/include" "${NATIVE}/vita3k/vita3k/rtc/include"
    "${NATIVE}/vita3k/vita3k/display/include"
    "${NATIVE}/vita3k/vita3k/renderer/include"
    "${EXTERNAL}/miniz" "${EXTERNAL}/boost" "${EXTERNAL}/fmt/include"
    "${EXTERNAL}/vita-toolchain/src"
    "${EXTERNAL}/googletest/googletest/include" "${EXTERNAL}/googletest/googletest")
target_compile_definitions(state-container-tests PRIVATE
    FMT_HEADER_ONLY BOOST_ALL_NO_LIB BOOST_FILESYSTEM_STATIC_LINK)
