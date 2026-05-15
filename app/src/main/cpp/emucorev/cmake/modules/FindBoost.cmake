set(BOOST_LOCAL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/boost")
set(BOOST_LOCAL_BINARY_DIR "${CMAKE_BINARY_DIR}/external/boost-cmake")

set(BOOST_SUPERPROJECT_VERSION "1.81.0")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

function(emucorev_add_boost_lib relative_path)
    get_filename_component(boost_lib_name "${relative_path}" NAME)
    if(NOT EXISTS "${BOOST_LOCAL_SOURCE_DIR}/${relative_path}/CMakeLists.txt")
        message(FATAL_ERROR "Vendored Boost CMake target is missing: ${BOOST_LOCAL_SOURCE_DIR}/${relative_path}")
    endif()

    add_subdirectory(
        "${BOOST_LOCAL_SOURCE_DIR}/${relative_path}"
        "${BOOST_LOCAL_BINARY_DIR}/${boost_lib_name}"
        EXCLUDE_FROM_ALL
    )
endfunction()

function(emucorev_add_legacy_boost_header_lib target_name)
    if(TARGET "Boost::${target_name}")
        return()
    endif()

    set(local_target "boost_${target_name}")
    add_library("${local_target}" INTERFACE)
    add_library("Boost::${target_name}" ALIAS "${local_target}")
    target_include_directories("${local_target}" INTERFACE "${BOOST_LOCAL_SOURCE_DIR}")
endfunction()

if(NOT TARGET Boost::filesystem)
    emucorev_add_boost_lib("libs/headers")
    target_include_directories(boost_headers INTERFACE "${BOOST_LOCAL_SOURCE_DIR}")
    if(NOT TARGET Boost::boost)
        add_library(Boost::boost ALIAS boost_headers)
    endif()

    emucorev_add_boost_lib("libs/align")
    emucorev_add_boost_lib("libs/assert")
    emucorev_add_boost_lib("libs/concept_check")
    emucorev_add_boost_lib("libs/config")
    emucorev_add_boost_lib("libs/container_hash")
    emucorev_add_boost_lib("libs/core")
    emucorev_add_boost_lib("libs/describe")
    emucorev_add_boost_lib("libs/detail")
    emucorev_add_boost_lib("libs/io")
    emucorev_add_boost_lib("libs/iterator")
    emucorev_add_boost_lib("libs/mp11")
    emucorev_add_boost_lib("libs/mpl")
    emucorev_add_boost_lib("libs/optional")
    emucorev_add_boost_lib("libs/predef")
    emucorev_add_boost_lib("libs/preprocessor")
    emucorev_add_boost_lib("libs/scope")
    emucorev_add_boost_lib("libs/smart_ptr")
    emucorev_add_boost_lib("libs/static_assert")
    emucorev_add_boost_lib("libs/throw_exception")
    emucorev_add_boost_lib("libs/type_traits")
    emucorev_add_boost_lib("libs/utility")
    emucorev_add_boost_lib("libs/variant2")
    emucorev_add_boost_lib("libs/winapi")
    emucorev_add_boost_lib("libs/function_types")

    emucorev_add_legacy_boost_header_lib("tuple")
    target_link_libraries(boost_tuple INTERFACE Boost::config Boost::core Boost::detail Boost::static_assert Boost::type_traits)

    emucorev_add_legacy_boost_header_lib("typeof")
    target_link_libraries(boost_typeof INTERFACE Boost::config Boost::mpl Boost::preprocessor Boost::type_traits)

    emucorev_add_legacy_boost_header_lib("functional")
    target_link_libraries(boost_functional INTERFACE Boost::config Boost::core Boost::type_traits)

    emucorev_add_boost_lib("libs/fusion")
    emucorev_add_boost_lib("libs/system")
    emucorev_add_boost_lib("libs/atomic")
    emucorev_add_boost_lib("libs/filesystem")
endif()

set(Boost_FOUND TRUE)
set(Boost_FILESYSTEM_FOUND TRUE)
set(Boost_VERSION 108100)
set(Boost_VERSION_STRING "1.81.0")
set(Boost_INCLUDE_DIR "${BOOST_LOCAL_SOURCE_DIR}")
set(Boost_INCLUDE_DIRS "${BOOST_LOCAL_SOURCE_DIR}")
set(Boost_LIBRARIES Boost::filesystem)
