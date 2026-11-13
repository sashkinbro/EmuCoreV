# Android NDK ships zlib in its sysroot, but CMake's generic finder can miss
# it when the app selects the NDK root path for dependencies. Resolve only the
# NDK-provided library for Android; use CMake's stock finder on host builds.
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
    include("${CMAKE_ROOT}/Modules/FindZLIB.cmake")
    return()
endif()

set(_emucorev_zlib_triple "")
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(_emucorev_zlib_triple "aarch64-linux-android")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(_emucorev_zlib_triple "arm-linux-androideabi")
elseif(ANDROID_ABI STREQUAL "x86")
    set(_emucorev_zlib_triple "i686-linux-android")
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(_emucorev_zlib_triple "x86_64-linux-android")
else()
    set(ZLIB_FOUND FALSE)
    return()
endif()

set(_emucorev_zlib_include_hints)
set(_emucorev_zlib_library_hints)
if(CMAKE_SYSROOT)
    list(APPEND _emucorev_zlib_include_hints "${CMAKE_SYSROOT}/usr/include")
    list(APPEND _emucorev_zlib_library_hints
        "${CMAKE_SYSROOT}/usr/lib/${_emucorev_zlib_triple}"
        "${CMAKE_SYSROOT}/usr/lib/${_emucorev_zlib_triple}/${ANDROID_PLATFORM_LEVEL}")
endif()
# Some Android app builds chain through a vcpkg toolchain instead of the NDK
# toolchain file, leaving CMAKE_SYSROOT empty. In that case, use only the
# compiler's target implicit paths (which already encode NDK ABI and API).
list(APPEND _emucorev_zlib_include_hints
    ${CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES}
    ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
list(APPEND _emucorev_zlib_library_hints
    ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
    ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
find_path(ZLIB_INCLUDE_DIR NAMES zlib.h
    PATHS ${_emucorev_zlib_include_hints}
    NO_DEFAULT_PATH)
if(NOT ZLIB_INCLUDE_DIR OR NOT EXISTS "${ZLIB_INCLUDE_DIR}/zlib.h")
    set(ZLIB_FOUND FALSE)
    return()
endif()

find_library(ZLIB_LIBRARY NAMES z
    PATHS ${_emucorev_zlib_library_hints}
    NO_DEFAULT_PATH)
if(NOT ZLIB_LIBRARY)
    set(ZLIB_FOUND FALSE)
    return()
endif()

file(STRINGS "${ZLIB_INCLUDE_DIR}/zlib.h" _emucorev_zlib_version_line
    REGEX "^#define ZLIB_VERSION \"[0-9]+(\\.[0-9]+)+")
if(_emucorev_zlib_version_line MATCHES "\"([0-9]+(\\.[0-9]+)+)")
    set(ZLIB_VERSION_STRING "${CMAKE_MATCH_1}")
    set(ZLIB_VERSION "${ZLIB_VERSION_STRING}")
else()
    set(ZLIB_FOUND FALSE)
    return()
endif()

set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR}")
set(ZLIB_LIBRARIES "${ZLIB_LIBRARY}")
set(ZLIB_FOUND TRUE)
if(NOT TARGET ZLIB::ZLIB)
    add_library(ZLIB::ZLIB UNKNOWN IMPORTED GLOBAL)
    set_target_properties(ZLIB::ZLIB PROPERTIES
        IMPORTED_LOCATION "${ZLIB_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZLIB_INCLUDE_DIR}")
endif()
