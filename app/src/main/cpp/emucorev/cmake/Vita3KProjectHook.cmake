get_filename_component(EMUCOREV_LAYER_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

list(PREPEND CMAKE_MODULE_PATH "${EMUCOREV_LAYER_DIR}/cmake/modules")

if(ANDROID_ABI STREQUAL "arm64-v8a" AND NOT DEFINED ARCHITECTURE)
    set(ARCHITECTURE "arm64" CACHE STRING "Target architecture for bundled projects")
endif()

set(CMAKE_SKIP_INSTALL_RULES TRUE CACHE BOOL "Android Gradle builds do not use CMake install rules" FORCE)

if(NOT DEFINED EMUCOREV_VITA3K_PROJECT_HOOK_DEFERRED)
    set(EMUCOREV_VITA3K_PROJECT_HOOK_DEFERRED TRUE)
    cmake_language(
        DEFER
        DIRECTORY "${CMAKE_SOURCE_DIR}"
        CALL include
            "${EMUCOREV_LAYER_DIR}/cmake/AttachToVita3K.cmake"
    )
endif()
