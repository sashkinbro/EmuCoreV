cmake_minimum_required(VERSION 3.20)
project(emucorev_core_host_tests C CXX)

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

add_executable(core_host_tests
    object_store_tests.cpp
    video_snapshot_tests.cpp
    avc_picture_tests.cpp
    guest_atomics_tests.cpp
    clib_chk_tests.cpp
    rwlock_policy_tests.cpp
    async_io_tests.cpp
    stub_dialog_tests.cpp
    shader_variant_key_tests.cpp
    compile_gate_tests.cpp
    transfer_fill_tests.cpp
    rtc_text_tests.cpp
    ngs_dsp_tests.cpp
    ngs_effects_tests.cpp
    ngs_scheduler_tests.cpp
    hle_stub_batch3_tests.cpp
    hle_stub_batch4_tests.cpp
    hle_stub_wave1_tests.cpp
    rear_touch_tests.cpp
    save_data_write_tests.cpp
    fragment_output_tests.cpp
    surface_policy_tests.cpp
    texture_policy_tests.cpp
    camera_mic_tests.cpp
    shader_cache_file_tests.cpp
    session_lifecycle_tests.cpp
    pacing_tests.cpp
    profile_overrides_tests.cpp)
# xxhash.c is compiled in; shader_cache_file.h only needs its declarations
target_sources(core_host_tests PRIVATE "${ECV_ROOT}/external/xxHash/xxhash.c")
target_include_directories(core_host_tests PRIVATE "${ECV_ROOT}/external/xxHash")
# profile_overrides.h (per-game profile field table) needs pugixml
target_sources(core_host_tests PRIVATE "${ECV_ROOT}/external/pugixml/src/pugixml.cpp")
target_include_directories(core_host_tests PRIVATE "${ECV_ROOT}/external/pugixml/src" "${CORE_ROOT}/config/include")
target_include_directories(core_host_tests PRIVATE
    "${CORE_ROOT}/kernel/include"
    "${CORE_ROOT}/cpu/include"
    "${CORE_ROOT}/ngs/include"
    "${CORE_ROOT}/io/include"
    "${CORE_ROOT}/dialog/include"
    "${CORE_ROOT}/renderer/include"
    "${CORE_ROOT}/shader/include"
    "${CORE_ROOT}/gxm/include"
    "${CORE_ROOT}/util/include"
    "${CORE_ROOT}/mem/include"
    "${CORE_ROOT}/rtc/include"
    "${CORE_ROOT}/audio/include"
    "${CORE_ROOT}/camera/include"
    "${CORE_ROOT}/touch/include"
    "${CORE_ROOT}/net/include"
    "${CORE_ROOT}/modules/SceAppUtil"
    "${CORE_ROOT}/modules/SceCodecEngine"
    "${CORE_ROOT}/modules/SceKernelModulemgr"
    "${CORE_ROOT}/modules/ScePower"
    "${CORE_ROOT}/modules/SceVideodec"
    "${CORE_ROOT}/codec/include"
    "${CORE_ROOT}/modules/SceLibKernel")
target_link_libraries(core_host_tests PRIVATE gtest_host)
target_compile_options(core_host_tests PRIVATE -Wall -Wextra)

# ATRAC9: the vendored LibAtrac9 and the real decoder wrapper (codec/src/atrac9.cpp) with a stub log header,
# plus the header-only NGS voice input validation. Run with run-core-host-tests.ps1 -Target atrac9_host_tests.
set(LIBATRAC9_SRC "${ECV_ROOT}/external/LibAtrac9/C/src")
file(GLOB LIBATRAC9_SOURCES "${LIBATRAC9_SRC}/*.c")
add_executable(atrac9_host_tests
    atrac9_decoder_tests.cpp
    "${CORE_ROOT}/codec/src/atrac9.cpp"
    ${LIBATRAC9_SOURCES})
target_include_directories(atrac9_host_tests BEFORE PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/atrac9_shim"
    "${CORE_ROOT}/codec/include"
    "${CORE_ROOT}/ngs/include"
    "${LIBATRAC9_SRC}")
target_link_libraries(atrac9_host_tests PRIVATE gtest_host)
target_compile_options(atrac9_host_tests PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
add_test(NAME atrac9_host_tests COMMAND atrac9_host_tests)

enable_testing()
add_test(NAME core_host_tests COMMAND core_host_tests)

# Save-state tests: compile the adapter's real save-state code (validators, IO restore) and the
# ECV pieces it needs against real .ecvs files. Everything the code reaches but the tests never
# execute (renderer, kernel, guest memory) stays unresolved, so this target is linked with
# /force:unresolved. Build and run it with run-core-host-tests.ps1 -Target savestate_host_tests.
# Not part of "all": it needs the lab artifacts (EMUCOREV_LAB) for most of its tests.
set(ADAPTER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp/emucorev")
set(VK_HEADERS "${ECV_ROOT}/external/VulkanMemoryAllocator-Hpp/Vulkan-Headers/include")
file(GLOB BOOST_LIB_INCLUDES "${ECV_ROOT}/external/boost/libs/*/include")
file(GLOB BOOST_FS_SOURCES "${ECV_ROOT}/external/boost/libs/filesystem/src/*.cpp")
set(SS_SRC "${ADAPTER_ROOT}/src/savestate")
set(RENDERER_SRC "${CORE_ROOT}/renderer/src")
add_library(boost_fs_host STATIC ${BOOST_FS_SOURCES})
target_include_directories(boost_fs_host PRIVATE "${ECV_ROOT}/external/boost" ${BOOST_LIB_INCLUDES})
target_compile_definitions(boost_fs_host PRIVATE BOOST_FILESYSTEM_SOURCE BOOST_FILESYSTEM_STATIC_LINK=1 _WIN32_WINNT=0x0A00)
add_library(ss_third_party_host STATIC
    "${ECV_ROOT}/external/miniz/miniz.c"
    "${ECV_ROOT}/external/fmt/src/format.cc"
    "${ECV_ROOT}/external/spdlog/src/spdlog.cpp"
    "${ECV_ROOT}/external/spdlog/src/async.cpp"
    "${ECV_ROOT}/external/spdlog/src/color_sinks.cpp"
    "${ECV_ROOT}/external/spdlog/src/file_sinks.cpp"
    "${ECV_ROOT}/external/spdlog/src/stdout_sinks.cpp"
    "${ECV_ROOT}/external/spdlog/src/cfg.cpp")
set(SS_INCLUDES
    "${ADAPTER_ROOT}/include"
    "${CORE_ROOT}" "${CORE_ROOT}/android/jni" "${CORE_ROOT}/modules" "${CORE_ROOT}/modules/SceGxm"
    "${ECV_ROOT}/external/boost" ${BOOST_LIB_INCLUDES}
    "${ECV_ROOT}/external/fmt/include" "${ECV_ROOT}/external/spdlog/include"
    "${ECV_ROOT}/external/miniz" "${ECV_ROOT}/external/xxHash/cmake_unofficial/.."
    "${ECV_ROOT}/external/vita-toolchain/src" "${ECV_ROOT}/external/concurrentqueue"
    "${ECV_ROOT}/external/sdl/include" "${ECV_ROOT}/external/pugixml/src"
    "${ECV_ROOT}/external/CppCommon" "${ECV_ROOT}/external/imgui" "${ECV_ROOT}/external/stb"
    "${ECV_ROOT}/external/glad/include"
    "${VK_HEADERS}"
    "${ECV_ROOT}/external/VulkanMemoryAllocator-Hpp/include"
    "${ECV_ROOT}/external/VulkanMemoryAllocator-Hpp/VulkanMemoryAllocator/include"
    "${ECV_ROOT}/external/psvpfstools/psvpfsparser/cmake/.." "${ECV_ROOT}/external/psvpfstools/zlib"
    "${ECV_ROOT}/external/psvpfstools/libb64/include" "${ECV_ROOT}/external/psvpfstools/libzrif/include")
file(GLOB SS_CORE_INCLUDES "${CORE_ROOT}/*/include")
list(APPEND SS_INCLUDES ${SS_CORE_INCLUDES})
set(SS_DEFINES BOOST_FILESYSTEM_NO_LIB BOOST_FILESYSTEM_STATIC_LINK=1 SPDLOG_COMPILED_LIB SPDLOG_FMT_EXTERNAL
    SPDLOG_NO_THREAD_ID EMUCOREV_ANDROID_BRIDGE=1)
target_include_directories(ss_third_party_host PRIVATE ${SS_INCLUDES})
target_compile_definitions(ss_third_party_host PRIVATE ${SS_DEFINES})
add_executable(savestate_host_tests EXCLUDE_FROM_ALL
    savestate_tests.cpp
    "${SS_SRC}/memory_image.cpp" "${SS_SRC}/gxm_state_io.cpp" "${SS_SRC}/gpu_state_io.cpp" "${SS_SRC}/gpu_buffer_stream.cpp"
    "${RENDERER_SRC}/command_snapshot.cpp" "${RENDERER_SRC}/finish.cpp"
    "${RENDERER_SRC}/vulkan/context_snapshot.cpp" "${RENDERER_SRC}/vulkan/surface_cache_snapshot.cpp"
    "${RENDERER_SRC}/vulkan/surface_cache_snapshot_codec.cpp" "${CORE_ROOT}/vkutil/src/image_snapshot.cpp"
    "${CORE_ROOT}/gxm/src/color.cpp" "${CORE_ROOT}/modules/SceGxm/SceGxm.cpp"
    "${CORE_ROOT}/io/src/device.cpp" "${CORE_ROOT}/io/src/file.cpp" "${CORE_ROOT}/io/src/filesystem.cpp"
    "${CORE_ROOT}/io/src/io.cpp" "${CORE_ROOT}/io/src/state_functions.cpp"
    "${CORE_ROOT}/util/src/string_utils.cpp" "${CORE_ROOT}/util/src/fs_utils.cpp")
target_sources(savestate_host_tests PRIVATE archive_host_shim.cpp)
target_include_directories(savestate_host_tests PRIVATE ${SS_INCLUDES})
target_compile_definitions(savestate_host_tests PRIVATE ${SS_DEFINES})
set_target_properties(savestate_host_tests PROPERTIES CXX_STANDARD 23) # kernel headers use std::expected
target_link_libraries(savestate_host_tests PRIVATE gtest_host boost_fs_host ss_third_party_host)
target_link_options(savestate_host_tests PRIVATE -static -Wl,/force:unresolved)

# Surface-cache snapshot layout compatibility (B130: 20 -> 64 slots). Only the codec is compiled.
# Build and run with run-core-host-tests.ps1 -Target surface_codec_host_tests.
add_executable(surface_codec_host_tests EXCLUDE_FROM_ALL
    surface_codec_compat_tests.cpp
    "${RENDERER_SRC}/vulkan/surface_cache_snapshot_codec.cpp" "${CORE_ROOT}/vkutil/src/image_snapshot.cpp")
target_include_directories(surface_codec_host_tests PRIVATE ${SS_INCLUDES})
target_compile_definitions(surface_codec_host_tests PRIVATE ${SS_DEFINES})
set_target_properties(surface_codec_host_tests PROPERTIES CXX_STANDARD 23)
target_link_libraries(surface_codec_host_tests PRIVATE gtest_host boost_fs_host ss_third_party_host)
target_link_options(surface_codec_host_tests PRIVATE -static -Wl,/force:unresolved)

# B132 render-pass / layout-table sync masks (vulkan.hpp structs only, no device).
# Build and run with run-core-host-tests.ps1 -Target render_sync_host_tests.
add_executable(render_sync_host_tests EXCLUDE_FROM_ALL
    render_pass_sync_tests.cpp sync_tracker_tests.cpp gpu_caps_tests.cpp render_pass_policy_tests.cpp pipeline_cache_file_tests.cpp gpu_policy_golden_tests.cpp
    "${CORE_ROOT}/vkutil/src/vkutil.cpp" "${CORE_ROOT}/vkutil/src/sync_tracker.cpp")
target_include_directories(render_sync_host_tests PRIVATE ${SS_INCLUDES})
target_compile_definitions(render_sync_host_tests PRIVATE ${SS_DEFINES})
set_target_properties(render_sync_host_tests PROPERTIES CXX_STANDARD 23)
target_link_libraries(render_sync_host_tests PRIVATE gtest_host boost_fs_host ss_third_party_host)
target_link_options(render_sync_host_tests PRIVATE -static -Wl,/force:unresolved)

# Buildout host tests: P7 colour format model, P8 visibility, P12 transfers, P15 rasteriser odds (header-only models).
# Build and run with run-core-host-tests.ps1 -Target buildout_host_tests.
add_executable(buildout_host_tests EXCLUDE_FROM_ALL
    colour_format_tests.cpp visibility_model_tests.cpp raster_model_tests.cpp transfer_model_tests.cpp buffer_store_model_tests.cpp "${CORE_ROOT}/vkutil/src/vkutil.cpp")
target_include_directories(buildout_host_tests PRIVATE ${SS_INCLUDES})
target_compile_definitions(buildout_host_tests PRIVATE ${SS_DEFINES})
set_target_properties(buildout_host_tests PROPERTIES CXX_STANDARD 23)
target_link_libraries(buildout_host_tests PRIVATE gtest_host boost_fs_host ss_third_party_host)
target_link_options(buildout_host_tests PRIVATE -static -Wl,/force:unresolved)
