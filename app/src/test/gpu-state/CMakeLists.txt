cmake_minimum_required(VERSION 3.22)
project(EmuCoreVGpuStateTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
get_filename_component(NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp" ABSOLUTE)
set(EXTERNAL "${NATIVE}/vita3k/external")
add_executable(gpu-state-tests image_snapshot_tests.cpp
    surface_cache_snapshot_tests.cpp
    allocator.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/test_logging.cpp"
    "${NATIVE}/vita3k/vita3k/renderer/src/vulkan/surface_cache_snapshot_codec.cpp"
    "${NATIVE}/vita3k/vita3k/vkutil/src/image_snapshot.cpp"
    "${NATIVE}/vita3k/vita3k/vkutil/src/objects.cpp"
    "${NATIVE}/vita3k/vita3k/vkutil/src/vkutil.cpp"
    "${EXTERNAL}/googletest/googletest/src/gtest-all.cc"
    "${EXTERNAL}/googletest/googletest/src/gtest_main.cc")
target_include_directories(gpu-state-tests PRIVATE
    "${NATIVE}/vita3k/vita3k/renderer/include"
    "${NATIVE}/vita3k/vita3k/gxm/include"
    "${NATIVE}/vita3k/vita3k/mem/include"
    "${NATIVE}/vita3k/vita3k/display/include"
    "${NATIVE}/vita3k/vita3k/emuenv/include"
    "${NATIVE}/vita3k/vita3k/kernel/include"
    "${NATIVE}/vita3k/vita3k/cpu/include"
    "${NATIVE}/vita3k/vita3k/rtc/include"
    "${NATIVE}/vita3k/vita3k/config/include"
    "${NATIVE}/vita3k/vita3k/input/include"
    "${NATIVE}/vita3k/vita3k/threads/include"
    "${NATIVE}/vita3k/vita3k/vkutil/include"
    "${EXTERNAL}/vita-toolchain/src"
    "${EXTERNAL}/sdl/include"
    "${EXTERNAL}/stb"
    "${EXTERNAL}/fmt/include"
    "${EXTERNAL}/spdlog/include"
    "${NATIVE}/vita3k/vita3k/util/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include"
    "${EXTERNAL}/boost"
    "${EXTERNAL}/googletest/googletest/include" "${EXTERNAL}/googletest/googletest")
target_link_libraries(gpu-state-tests PRIVATE dl log)
target_link_options(gpu-state-tests PRIVATE -Wl,--gc-sections)
target_compile_definitions(gpu-state-tests PRIVATE FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
