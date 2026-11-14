# Include this file at the end of vita3k/vita3k/renderer/CMakeLists.txt, after
# `renderer` has been created. The caller remains the owner of the upstream CMake file.
set(_EMUCOREV_SHADER_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(_EMUCOREV_NATIVE_INCLUDE "${_EMUCOREV_SHADER_DIR}/../../include" ABSOLUTE)

target_sources(renderer PRIVATE
    "${_EMUCOREV_SHADER_DIR}/vulkan_shader_chain.cpp"
    "${_EMUCOREV_SHADER_DIR}/shader_preset_bridge.cpp"
)
target_include_directories(renderer PRIVATE "${_EMUCOREV_NATIVE_INCLUDE}")

if(ANDROID)
    set(EMUCOREV_HAVE_LIBRASHADER OFF)
    add_subdirectory(
        "${_EMUCOREV_SHADER_DIR}/librashader"
        "${CMAKE_CURRENT_BINARY_DIR}/emucorev-librashader"
    )
    if(EMUCOREV_HAVE_LIBRASHADER)
        target_compile_definitions(renderer PRIVATE EMUCOREV_HAVE_LIBRASHADER=1)
        target_link_libraries(renderer PRIVATE librashader)
        message(STATUS "EmuCoreV: RetroArch Slang shaders enabled via pinned librashader")
    else()
        message(STATUS "EmuCoreV: librashader unavailable; RetroArch shader controls must stay disabled")
    endif()
endif()
