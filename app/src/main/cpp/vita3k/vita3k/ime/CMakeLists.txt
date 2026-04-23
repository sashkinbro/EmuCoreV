add_library(
    ime
    STATIC
    include/ime/functions.h
    include/ime/state.h
    src/functions.cpp
)

target_include_directories(ime PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/vita3k/mem/include
    ${CMAKE_SOURCE_DIR}/vita3k/util/include
    ${CMAKE_SOURCE_DIR}/vita3k/lang/include
    ${CMAKE_SOURCE_DIR}/vita3k/compat/include
    ${CMAKE_SOURCE_DIR}/external/imgui
)
