cmake_minimum_required(VERSION 3.22)
project(EmuCoreVNativeTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(VENDOR "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp/vita3k" ABSOLUTE)
set(CORE "${VENDOR}/vita3k")
set(EXTERNAL "${VENDOR}/external")
set(ARCHITECTURE arm64)
add_subdirectory("${EXTERNAL}/ffmpeg" "external/ffmpeg-build")

# Build the real interpreter, parser, guest allocator and pure format
# translations without a rendering context. Android normally excludes
# Vita3K's native unit test targets.
file(GLOB BOOST_FILESYSTEM_SOURCES "${EXTERNAL}/boost/libs/filesystem/src/*.cpp")
add_executable(cheat-tests
    "${CORE}/cheat/tests/cheat_tests.cpp"
    "${CORE}/cheat/src/cheat.cpp"
    "${CORE}/cheat/src/parser.cpp"
    "${CORE}/mem/src/mem.cpp"
    "${CORE}/mem/src/allocator.cpp"
    "${CORE}/util/src/string_utils.cpp"
    "${CORE}/util/src/fs_utils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_logging.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/aac_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gxm_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/usse_analyzer_tests.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mempool_tests.cpp"
    "${CORE}/renderer/src/vulkan/gxm_to_vulkan.cpp"
    "${CORE}/shader/src/usse_dynamic_uniform.cpp"
    "${CORE}/shader/src/usse_decode_helpers.cpp"
    "${CORE}/gxm/src/color.cpp"
    "${CORE}/gxm/src/textures.cpp"
    "${CORE}/codec/src/aac.cpp"
    "${CORE}/codec/src/decoder.cpp"
    "${EXTERNAL}/googletest/googletest/src/gtest-all.cc"
    "${EXTERNAL}/googletest/googletest/src/gtest_main.cc"
    ${BOOST_FILESYSTEM_SOURCES}
)
target_include_directories(cheat-tests PRIVATE
    "${CORE}/cheat/include" "${CORE}/mem/include" "${CORE}/util/include"
    "${CORE}/codec/include"
    "${CORE}/renderer/include" "${CORE}/gxm/include" "${CORE}/shader/include"
    "${EXTERNAL}/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include"
    "${EXTERNAL}/boost" "${EXTERNAL}/fmt/include" "${EXTERNAL}/spdlog/include"
    "${EXTERNAL}/sdl/include" "${EXTERNAL}/googletest/googletest/include"
    "${EXTERNAL}/googletest/googletest"
)
target_compile_definitions(cheat-tests PRIVATE
    FMT_HEADER_ONLY SPDLOG_FMT_EXTERNAL BOOST_ALL_NO_LIB BOOST_FILESYSTEM_STATIC_LINK
)
target_link_libraries(cheat-tests PRIVATE ffmpeg)
if(ANDROID)
    target_compile_options(cheat-tests PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(cheat-tests PRIVATE -Wl,--gc-sections)
    target_link_libraries(cheat-tests PRIVATE log dl)
endif()
