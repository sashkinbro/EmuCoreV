set(BOOST_LOCAL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/external/boost")
set(BOOST_LOCAL_BINARY_DIR "${CMAKE_BINARY_DIR}/external/boost-cmake")

# Match the vendored Boost version closely enough for the per-library CMake files.
set(BOOST_SUPERPROJECT_VERSION "1.81.0")
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

if(TARGET Boost::filesystem)
    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "${BOOST_LOCAL_SOURCE_DIR}")
    set(Boost_LIBRARIES Boost::filesystem)
    return()
endif()

function(vita3k_add_boost_lib relative_path)
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

function(vita3k_add_legacy_boost_header_lib target_name)
    if(TARGET "Boost::${target_name}")
        return()
    endif()

    set(local_target "boost_${target_name}")
    add_library("${local_target}" INTERFACE)
    add_library("Boost::${target_name}" ALIAS "${local_target}")
    target_include_directories("${local_target}" INTERFACE "${BOOST_LOCAL_SOURCE_DIR}")
endfunction()

vita3k_add_boost_lib("libs/headers")
target_include_directories(boost_headers INTERFACE "${BOOST_LOCAL_SOURCE_DIR}")
if(NOT TARGET Boost::boost)
    add_library(Boost::boost ALIAS boost_headers)
endif()

vita3k_add_boost_lib("libs/align")
vita3k_add_boost_lib("libs/assert")
vita3k_add_boost_lib("libs/concept_check")
vita3k_add_boost_lib("libs/config")
vita3k_add_boost_lib("libs/container_hash")
vita3k_add_boost_lib("libs/core")
vita3k_add_boost_lib("libs/describe")
vita3k_add_boost_lib("libs/detail")
vita3k_add_boost_lib("libs/io")
vita3k_add_boost_lib("libs/iterator")
vita3k_add_boost_lib("libs/mp11")
vita3k_add_boost_lib("libs/mpl")
vita3k_add_boost_lib("libs/optional")
vita3k_add_boost_lib("libs/predef")
vita3k_add_boost_lib("libs/preprocessor")
vita3k_add_boost_lib("libs/scope")
vita3k_add_boost_lib("libs/smart_ptr")
vita3k_add_boost_lib("libs/static_assert")
vita3k_add_boost_lib("libs/throw_exception")
vita3k_add_boost_lib("libs/type_traits")
vita3k_add_boost_lib("libs/utility")
vita3k_add_boost_lib("libs/variant2")
vita3k_add_boost_lib("libs/winapi")
vita3k_add_boost_lib("libs/function_types")

vita3k_add_legacy_boost_header_lib("tuple")
target_link_libraries(boost_tuple INTERFACE Boost::config Boost::core Boost::detail Boost::static_assert Boost::type_traits)

vita3k_add_legacy_boost_header_lib("typeof")
target_link_libraries(boost_typeof INTERFACE Boost::config Boost::mpl Boost::preprocessor Boost::type_traits)

vita3k_add_legacy_boost_header_lib("functional")
target_link_libraries(boost_functional INTERFACE Boost::config Boost::core Boost::type_traits)

vita3k_add_boost_lib("libs/fusion")
vita3k_add_boost_lib("libs/system")
vita3k_add_boost_lib("libs/atomic")
vita3k_add_boost_lib("libs/filesystem")

set(Boost_FOUND TRUE)
set(Boost_INCLUDE_DIRS "${BOOST_LOCAL_SOURCE_DIR}")
set(Boost_LIBRARIES Boost::filesystem)
