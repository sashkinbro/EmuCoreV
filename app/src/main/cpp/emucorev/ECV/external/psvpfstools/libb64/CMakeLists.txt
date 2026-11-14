cmake_minimum_required(VERSION 3.10)

set (PROJECT libb64)
project (${PROJECT})

add_library(${PROJECT} STATIC src/cencode.c 
                              src/cdecode.c 
                              include/b64/cdecode.h 
                              include/b64/cencode.h 
                              include/b64/cdecode.h)

include_directories(include)
