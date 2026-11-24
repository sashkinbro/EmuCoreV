cmake_minimum_required(VERSION 3.20)
project(fwinstall_host LANGUAGES C CXX)

# Host (Windows/llvm-mingw or Linux) test harness for the firmware installer: builds the same core sources the app links
# (packages/sce_utils.cpp, exfat.cpp, pup.cpp, util) plus the EmuCoreV adapter installer, against a host OpenSSL.
# Use run_fwinstall_host_tests.py; see README.md. Nothing here is part of the Android build.
get_filename_component(ECV_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(ECV_BASE "${ECV_ROOT}/app/src/main/cpp/emucorev")
set(ECV_CORE "${ECV_BASE}/ECV/ECV")
set(ECV_EXT "${ECV_BASE}/ECV/external")
set(OSSL_DIR "C:/Users/SBRO/emucorev-lab/tools/ossl/openssl-3.3.2" CACHE PATH "OpenSSL 3.x static build (libcrypto.a + include)")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g1")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1")

add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES IMPORTED_LOCATION "${OSSL_DIR}/libcrypto.a"
  INTERFACE_INCLUDE_DIRECTORIES "${OSSL_DIR}/include")
if(WIN32)
  set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES "ws2_32;gdi32;crypt32;advapi32;user32;bcrypt")
endif()

add_subdirectory("${ECV_EXT}/libfat16" fat16 EXCLUDE_FROM_ALL)
add_library(miniz STATIC "${ECV_EXT}/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${ECV_EXT}/miniz")
target_compile_options(miniz PRIVATE -w)

set(CORE_INCLUDES
  "${ECV_CORE}/util/include" "${ECV_CORE}/packages/include" "${ECV_CORE}/mem/include" "${ECV_CORE}/config/include"
  "${CMAKE_CURRENT_LIST_DIR}/shim" "${ECV_CORE}" "${OSSL_DIR}/include"
  "${ECV_BASE}/include" "${ECV_BASE}/src"
  "${ECV_EXT}/fmt/include" "${ECV_EXT}/spdlog/include" "${ECV_EXT}/boost" "${ECV_EXT}/miniz"
  "${ECV_EXT}/libfat16/include" "${ECV_EXT}/xxHash" "${ECV_EXT}/sdl/include" "${ECV_EXT}/vita-toolchain/src")

file(GLOB BOOST_FS_SRC CONFIGURE_DEPENDS "${ECV_EXT}/boost/libs/filesystem/src/*.cpp")
set(UTIL_SRC
  "${ECV_CORE}/util/src/fs_utils.cpp" "${ECV_CORE}/util/src/hash.cpp" "${ECV_CORE}/util/src/logging.cpp"
  "${ECV_CORE}/util/src/string_utils.cpp" "${ECV_CORE}/util/src/byte.cpp")
set(SPDLOG_SRC "${ECV_EXT}/spdlog/src/spdlog.cpp" "${ECV_EXT}/spdlog/src/stdout_sinks.cpp" "${ECV_EXT}/spdlog/src/color_sinks.cpp"
  "${ECV_EXT}/spdlog/src/file_sinks.cpp" "${ECV_EXT}/spdlog/src/async.cpp" "${ECV_EXT}/spdlog/src/cfg.cpp")
file(GLOB FMT_SRC "${ECV_EXT}/fmt/src/format.cc" "${ECV_EXT}/fmt/src/os.cc")
set(PACKAGES_SRC "${ECV_CORE}/packages/src/exfat.cpp" "${ECV_CORE}/packages/src/sce_utils.cpp" "${ECV_CORE}/packages/src/pup.cpp")

set(COMMON_SRC host_stubs.cpp ${PACKAGES_SRC} ${UTIL_SRC} ${SPDLOG_SRC} ${FMT_SRC} ${BOOST_FS_SRC})

function(fw_target name)
  add_executable(${name} ${ARGN} ${COMMON_SRC})
  target_include_directories(${name} PRIVATE ${CORE_INCLUDES})
  target_compile_definitions(${name} PRIVATE SPDLOG_WCHAR_FILENAMES FMT_CONSTEVAL= SPDLOG_FMT_EXTERNAL SPDLOG_COMPILED_LIB SPDLOG_NO_THREAD_ID
    BOOST_FILESYSTEM_NO_LIB BOOST_FILESYSTEM_STATIC_LINK=1 NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(${name} PRIVATE -w -O2 -fms-extensions "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include iterator>")
  target_link_libraries(${name} PRIVATE FAT16 miniz OpenSSL::Crypto)
  target_link_options(${name} PRIVATE -static)
endfunction()

# Reference: the vendored install_pup exactly as shipped (the oracle the new installer must match byte for byte).
fw_target(fw_ref ref_install.cpp)
fw_target(fw_tests fw_tests.cpp "${ECV_BASE}/src/firmware_installer.cpp")
