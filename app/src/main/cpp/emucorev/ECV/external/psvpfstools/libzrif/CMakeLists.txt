cmake_minimum_required (VERSION 3.6.0)

set (PROJECT libzRIF)
project (${PROJECT})

add_library(${PROJECT} STATIC include/zRIF/keyflate.h
                              include/zRIF/licdec.h
                              include/zRIF/rif.h
                              src/keyflate.c
                              src/licdec.cpp)

target_include_directories(${PROJECT} PUBLIC ${ZLIB_INCLUDE_DIR} ${LIBB64_INCLUDE_DIR} include)

