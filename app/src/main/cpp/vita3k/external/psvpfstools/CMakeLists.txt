cmake_minimum_required(VERSION 3.18)

find_package(ZLIB 1.2.11)
if(ZLIB_FOUND)
    add_library(zlib ALIAS ZLIB::ZLIB)
endif()

if(NOT ZLIB_FOUND)
    add_subdirectory(zlib)
    set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib")
    set(ZLIB_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/zlib/build/Debug/zlibstaticd.lib")
endif()

if(NOT TARGET OpenSSL::Crypto)
    find_package(OpenSSL 3 REQUIRED)
endif()

set(LIBB64_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libb64/include")
set(LIBZRIF_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libzrif/include")
set(PSVPFSPARSER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/psvpfsparser")

add_subdirectory(libb64)
add_subdirectory(libzrif)
add_subdirectory(psvpfsparser/cmake)
