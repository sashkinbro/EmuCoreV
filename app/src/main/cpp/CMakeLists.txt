cmake_minimum_required(VERSION 3.22.1)

project(EmuCoreVNative)

# Keep a local CMake entry point so Android Studio exposes the in-app cpp root,
# while the actual core now lives under this directory.
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/vita3k"
    "${CMAKE_BINARY_DIR}/vita3k-core"
)
