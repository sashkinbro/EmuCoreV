cmake_minimum_required(VERSION 3.22)
project(EmuCoreVNativeShell LANGUAGES C CXX)

add_subdirectory(.. native-ui)
add_library(native_app_glue STATIC "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC "${ANDROID_NDK}/sources/android/native_app_glue")
# Theme BGM decoder: LibAtrac9 (MIT), vendored with the Vita3K tree; only the
# codec sources are compiled, no emulator code.
set(LIBATRAC9_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp/emucorev/ECV/external/LibAtrac9/C/src")
file(GLOB LIBATRAC9_SOURCES "${LIBATRAC9_DIR}/*.c")
add_library(shell_atrac9 STATIC ${LIBATRAC9_SOURCES})
target_include_directories(shell_atrac9 PUBLIC "${LIBATRAC9_DIR}")
add_library(emucorev_shell SHARED src/main/cpp/shell.cpp src/main/cpp/at9_decode.cpp)
target_compile_features(emucorev_shell PRIVATE cxx_std_20)
target_compile_options(emucorev_shell PRIVATE -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off)
target_link_options(emucorev_shell PRIVATE "-uANativeActivity_onCreate")
target_link_libraries(emucorev_shell PRIVATE shell_atrac9 native_app_glue emucorev_paf emucorev_paf_gles android log EGL)
