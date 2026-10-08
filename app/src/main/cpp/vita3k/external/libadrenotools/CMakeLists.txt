cmake_minimum_required(VERSION 3.14)

if(NOT ${CMAKE_ANDROID_ARCH_ABI} STREQUAL arm64-v8a)
	message(FATAL_ERROR "Unsupported target architecture: ${CMAKE_ANDROID_ARCH_ABI}. Please make an issue on the repo!")
endif()

project(adrenotools LANGUAGES CXX C)

set(GEN_INSTALL_TARGET OFF CACHE BOOL "")

add_subdirectory(lib/linkernsbypass)

set(LIB_SOURCES src/bcenabler.cpp
                src/driver.cpp
                include/adrenotools/bcenabler.h
                include/adrenotools/driver.h
                include/adrenotools/priv.h)

add_library(adrenotools ${LIB_SOURCES})

# Generate the AArch64 patch with the selected Android toolchain. Keeping this
# in the build directory makes clean checkouts work on Windows as well as Unix.
set(BC_PATCH_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/bcenabler_patch.o")
set(BC_PATCH_BINARY "${CMAKE_CURRENT_BINARY_DIR}/bcenabler_patch.bin")
set(BC_PATCH_HEADER "${CMAKE_CURRENT_BINARY_DIR}/gen/bcenabler_patch.h")
add_custom_command(OUTPUT "${BC_PATCH_HEADER}"
    COMMAND "${CMAKE_C_COMPILER}" "--target=${CMAKE_C_COMPILER_TARGET}"
        -c "${CMAKE_CURRENT_SOURCE_DIR}/src/bcenabler_patch.s" -o "${BC_PATCH_OBJECT}"
    COMMAND "${CMAKE_OBJCOPY}" -O binary -j .text "${BC_PATCH_OBJECT}" "${BC_PATCH_BINARY}"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${BC_PATCH_BINARY}" "-DOUTPUT=${BC_PATCH_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BinaryToHeader.cmake"
    DEPENDS src/bcenabler_patch.s cmake/BinaryToHeader.cmake
    VERBATIM)
target_sources(adrenotools PRIVATE "${BC_PATCH_HEADER}")

target_include_directories(adrenotools PUBLIC include)
target_include_directories(adrenotools PRIVATE .)
target_include_directories(adrenotools BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_options(adrenotools PRIVATE -Wall -Wextra)
target_link_libraries(adrenotools android linkernsbypass)

add_subdirectory(src/hook)
if (${BUILD_SHARED_LIBS})
    target_link_options(adrenotools PRIVATE "-Wl,-s,--exclude-libs,liblinkernsbypass.a")
endif()
if (${GEN_INSTALL_TARGET})
  configure_file(adrenotools.pc.in adrenotools.pc @ONLY)
  install(FILES ${CMAKE_BINARY_DIR}/adrenotools.pc DESTINATION lib/pkgconfig)
  install(TARGETS adrenotools main_hook file_redirect_hook gsl_alloc_hook hook_impl)
  install(DIRECTORY include/
          DESTINATION include
          FILES_MATCHING PATTERN "*.h*")
endif()
