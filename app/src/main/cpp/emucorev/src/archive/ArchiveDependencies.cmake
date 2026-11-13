# Include from the Android project's normal configure phase after vita3k exists.
# Fetch pinned libarchive/xz sources and apply the committed decoder caps.
function(emucorev_add_archive_dependencies)
    include(FetchContent)
    # DOWNLOAD_EXTRACT_TIMESTAMP was introduced in CMake 3.24. Android's
    # supported CMake 3.22 treats it as part of URL_HASH instead.
    set(_archive_timestamp_args)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
        list(APPEND _archive_timestamp_args DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    find_program(EMUCOREV_PATCH_EXECUTABLE NAMES patch patch.exe)
    if(NOT EMUCOREV_PATCH_EXECUTABLE)
        find_package(Git REQUIRED)
        get_filename_component(_git_cmd_dir "${GIT_EXECUTABLE}" DIRECTORY)
        get_filename_component(_git_root "${_git_cmd_dir}/.." ABSOLUTE)
        find_program(EMUCOREV_PATCH_EXECUTABLE NAMES patch.exe
            PATHS "${_git_root}/usr/bin" NO_DEFAULT_PATH)
    endif()
    if(NOT EMUCOREV_PATCH_EXECUTABLE)
        message(FATAL_ERROR "A POSIX patch utility is required to apply the pinned libarchive caps")
    endif()
    set(_archive_patch "${EMUCOREV_BRIDGE_DIR}/src/archive/patches/0001-android-decoder-resource-caps.patch")
    FetchContent_Declare(emucorev_libarchive
        URL "https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.gz"
        URL_HASH SHA256=900fe9fc7237c2b04f090f3c3648de8fe597525771f5035016537ce2d667d12b
        ${_archive_timestamp_args})
    FetchContent_Declare(emucorev_xz
        URL "https://github.com/tukaani-project/xz/releases/download/v5.8.3/xz-5.8.3.tar.gz"
        URL_HASH SHA256=3d3a1b973af218114f4f889bbaa2f4c037deaae0c8e815eec381c3d546b974a0
        ${_archive_timestamp_args})
    FetchContent_GetProperties(emucorev_libarchive)
    if(NOT emucorev_libarchive_POPULATED)
        FetchContent_Populate(emucorev_libarchive)
    endif()
    FetchContent_GetProperties(emucorev_xz)
    if(NOT emucorev_xz_POPULATED)
        FetchContent_Populate(emucorev_xz)
    endif()
    set(_libarchive_source "${emucorev_libarchive_SOURCE_DIR}")
    set(_xz_source "${emucorev_xz_SOURCE_DIR}")
    if(NOT EXISTS "${_archive_patch}")
        message(FATAL_ERROR "Committed libarchive cap patch is missing: ${_archive_patch}")
    endif()
    file(SHA256 "${_libarchive_source}/libarchive/archive_read_support_format_rar.c" _rar_hash)
    file(SHA256 "${_libarchive_source}/libarchive/archive_read_support_format_rar5.c" _rar5_hash)
    file(SHA256 "${_libarchive_source}/libarchive/archive_read_support_format_7zip.c" _seven_zip_hash)
    file(SHA256 "${_libarchive_source}/CMakeLists.txt" _archive_cmake_hash)
    if(_rar_hash STREQUAL "c9afc9af8b2f537f1ecaee4ce521208562b3c37aa103f7d87e8e656e2150ab88" AND
       _rar5_hash STREQUAL "0a21cc37ef7ae0f83bbffdac1b08414380787ddf1cddae7b8539985e4b28e3ab" AND
       _seven_zip_hash STREQUAL "3456c0e2d79c3a7fb2b611ca28617b11a146e4ae57d40523f6767fc52412c2e3" AND
       _archive_cmake_hash STREQUAL "7a8334ec78e49d08ff59b99dd88101b111e1dc5040f7b63e03d305af96232579")
        execute_process(
            COMMAND "${EMUCOREV_PATCH_EXECUTABLE}" --batch --forward -p1 -i "${_archive_patch}"
            WORKING_DIRECTORY "${_libarchive_source}"
            RESULT_VARIABLE _patch_result
            ERROR_VARIABLE _patch_error)
        if(NOT _patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply the pinned libarchive resource caps: ${_patch_error}")
        endif()
        execute_process(
            COMMAND "${EMUCOREV_PATCH_EXECUTABLE}" --batch --forward -p1 -i
                    "${EMUCOREV_BRIDGE_DIR}/src/archive/patches/0002-in-tree-liblzma-cmake.patch"
            WORKING_DIRECTORY "${_libarchive_source}"
            RESULT_VARIABLE _cmake_patch_result
            ERROR_VARIABLE _cmake_patch_error)
        if(NOT _cmake_patch_result EQUAL 0)
            message(FATAL_ERROR "Could not wire the pinned in-tree liblzma target: ${_cmake_patch_error}")
        endif()
    elseif(NOT _rar_hash STREQUAL "66c881b2d2b66454462bbf6f60da5e5e1aadc3e242cbf7beaa70c10d6bec867b" OR
           NOT _rar5_hash STREQUAL "c9f48645ece0adb27ce5a56ffbca4e30330994fbb6b8f29c45353b4d2a38adba" OR
           NOT _seven_zip_hash STREQUAL "f37bd51c6c66df0727763448339fe83a6e263270c343ee01b32a01d37bbbb38d" OR
           NOT _archive_cmake_hash STREQUAL "a4f64aa287adef564b9a02fe4674af521f49494f007943f3247911bd3b2ea197")
        message(FATAL_ERROR "Downloaded libarchive sources do not match the reviewed upstream/capped hashes")
    endif()
    set(_android_include "${_libarchive_source}/contrib/android/include")

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(XZ_NLS OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_XZ OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_XZDEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMADEC OFF CACHE BOOL "" FORCE)
    set(XZ_TOOL_LZMAINFO OFF CACHE BOOL "" FORCE)
    set(XZ_DOXYGEN OFF CACHE BOOL "" FORCE)
    set(XZ_DOC OFF CACHE BOOL "" FORCE)
    set(XZ_MICROLZMA_ENCODER OFF CACHE BOOL "" FORCE)
    set(XZ_MICROLZMA_DECODER OFF CACHE BOOL "" FORCE)
    set(ENABLE_TEST OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(ENABLE_BZip2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZMA ON CACHE BOOL "" FORCE)
    set(ENABLE_EXPAT OFF CACHE BOOL "" FORCE)
    set(ENABLE_LIBXML2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_NETTLE OFF CACHE BOOL "" FORCE)
    set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
    set(ENABLE_LZ4 OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)

    add_subdirectory("${_xz_source}" "${CMAKE_CURRENT_BINARY_DIR}/emucorev-xz")
    set(EMUCOREV_XZ_SOURCE_DIR "${_xz_source}")
    set(EMUCOREV_LZMA_MODULE_DIR "${EMUCOREV_BRIDGE_DIR}/src/archive/cmake")
    list(PREPEND CMAKE_MODULE_PATH "${EMUCOREV_LZMA_MODULE_DIR}")
    add_subdirectory("${_libarchive_source}" "${CMAKE_CURRENT_BINARY_DIR}/emucorev-libarchive")

    # Keep libarchive headers private: Vita3K also has an unrelated archive.h.
    set(_archive_adapter_sources "${EMUCOREV_BRIDGE_DIR}/src/archive/archive_expander.cpp")
    if(NOT EMUCOREV_ARCHIVE_SKIP_JNI)
        list(APPEND _archive_adapter_sources "${EMUCOREV_BRIDGE_DIR}/src/archive/archive_jni_bridge.cpp")
    endif()
    add_library(emucorev_archive_adapter OBJECT ${_archive_adapter_sources})
    target_compile_features(emucorev_archive_adapter PRIVATE cxx_std_17)
    target_include_directories(emucorev_archive_adapter PRIVATE
        "${EMUCOREV_BRIDGE_DIR}/include"
        "${_libarchive_source}/libarchive"
        "${CMAKE_CURRENT_BINARY_DIR}/emucorev-libarchive/libarchive")
    if(ANDROID)
        target_include_directories(emucorev_archive_adapter PRIVATE "${_android_include}")
    endif()
    target_link_libraries(emucorev_archive_adapter PRIVATE archive_static)
    target_sources(vita3k PRIVATE $<TARGET_OBJECTS:emucorev_archive_adapter>)
    set_property(TARGET archive_static PROPERTY INTERFACE_INCLUDE_DIRECTORIES "")
    target_link_libraries(vita3k PRIVATE archive_static)
endfunction()
