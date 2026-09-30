cmake_minimum_required(VERSION 3.22.1)

project(EmuCoreVNative)

# Google Play requires 16 KiB page size support: keep every ELF load segment aligned
# even when a toolchain default changes.
add_link_options("-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")

# Keep a local CMake entry point so Android Studio exposes the in-app cpp root,
# while the actual core now lives under this directory.
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/vita3k"
    "${CMAKE_BINARY_DIR}/vita3k-core"
)
