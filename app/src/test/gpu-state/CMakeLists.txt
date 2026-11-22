cmake_minimum_required(VERSION 3.22)
project(EmuCoreVGpuStateTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
get_filename_component(NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp" ABSOLUTE)
set(EXTERNAL "${NATIVE}/emucorev/ECV/external")
add_executable(gpu-state-tests image_snapshot_tests.cpp
    surface_cache_snapshot_tests.cpp
    surface_capability_selection_tests.cpp
    allocator.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../cpp/test_logging.cpp"
    "${NATIVE}/emucorev/ECV/ECV/renderer/src/vulkan/surface_cache_snapshot_codec.cpp"
    "${NATIVE}/emucorev/ECV/ECV/vkutil/src/image_snapshot.cpp"
    "${NATIVE}/emucorev/ECV/ECV/vkutil/src/objects.cpp"
    "${NATIVE}/emucorev/ECV/ECV/vkutil/src/vkutil.cpp"
    "${EXTERNAL}/googletest/googletest/src/gtest-all.cc"
    "${EXTERNAL}/googletest/googletest/src/gtest_main.cc")
target_include_directories(gpu-state-tests PRIVATE
    "${NATIVE}/emucorev/ECV/ECV/renderer/include"
    "${NATIVE}/emucorev/ECV/ECV/gxm/include"
    "${NATIVE}/emucorev/ECV/ECV/mem/include"
    "${NATIVE}/emucorev/ECV/ECV/display/include"
    "${NATIVE}/emucorev/ECV/ECV/emuenv/include"
    "${NATIVE}/emucorev/ECV/ECV/kernel/include"
    "${NATIVE}/emucorev/ECV/ECV/cpu/include"
    "${NATIVE}/emucorev/ECV/ECV/rtc/include"
    "${NATIVE}/emucorev/ECV/ECV/config/include"
    "${NATIVE}/emucorev/ECV/ECV/input/include"
    "${NATIVE}/emucorev/ECV/ECV/threads/include"
    "${NATIVE}/emucorev/ECV/ECV/vkutil/include"
    "${EXTERNAL}/vita-toolchain/src"
    "${EXTERNAL}/sdl/include"
    "${EXTERNAL}/stb"
    "${EXTERNAL}/fmt/include"
    "${EXTERNAL}/spdlog/include"
    "${NATIVE}/emucorev/ECV/ECV/util/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include"
    "${EXTERNAL}/boost"
    "${EXTERNAL}/googletest/googletest/include" "${EXTERNAL}/googletest/googletest")
target_link_libraries(gpu-state-tests PRIVATE dl log)
target_link_options(gpu-state-tests PRIVATE -Wl,--gc-sections)
target_compile_definitions(gpu-state-tests PRIVATE FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL)
