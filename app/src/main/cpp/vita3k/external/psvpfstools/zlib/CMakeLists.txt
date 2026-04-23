project(zlib)

add_library(zlib INTERFACE)

option(USE_SYSTEM_ZLIB "Dynamically link against system zlib" OFF)

if (ANDROID)
    set(USE_SYSTEM_ZLIB ON)
endif()

if (NOT USE_SYSTEM_ZLIB)
    target_include_directories(zlib INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
    if (WIN32 AND NOT MINGW)
        target_link_libraries(zlib INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/build/Debug/zlibstaticd.lib")
    elseif (APPLE)
        target_link_libraries(zlib INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/build/Debug/libmacz.a")
    elseif (UNIX)
        target_link_libraries(zlib INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/build/Debug/libz.a")
    else ()
	    set(USE_SYSTEM_ZLIB ON)
    endif ()
endif ()

if(USE_SYSTEM_ZLIB)
    if (ANDROID)
        target_link_libraries(zlib INTERFACE z)
    else()
        find_package(ZLIB REQUIRED)
        target_include_directories(zlib INTERFACE "${ZLIB_INCLUDE_DIRS}")
        target_link_libraries(zlib INTERFACE "${ZLIB_LIBRARIES}")
    endif()
endif()
