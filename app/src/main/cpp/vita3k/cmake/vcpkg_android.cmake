#
# vcpkg_android.cmake 
#
# Helper script when using vcpkg with cmake. It should be triggered via the variable VCPKG_TARGET_ANDROID
#
# For example:
# if (VCPKG_TARGET_ANDROID)
#     include("cmake/vcpkg_android.cmake")
# endif()
# 
# This script will:
# 1 & 2. check the presence of needed env variables: ANDROID_NDK_HOME and VCPKG_ROOT
# 3. set VCPKG_TARGET_TRIPLET according to ANDROID_ABI
# 4. Combine vcpkg and Android toolchains by setting CMAKE_TOOLCHAIN_FILE 
#    and VCPKG_CHAINLOAD_TOOLCHAIN_FILE

# Note: VCPKG_TARGET_ANDROID is not an official Vcpkg variable. 
# it is introduced for the need of this script

if (VCPKG_TARGET_ANDROID)

    #
    # 1. Check the presence of environment variable ANDROID_NDK_HOME
    #
    if (DEFINED ENV{ANDROID_NDK_HOME})
        set(ANDROID_NDK_HOME_VALUE "$ENV{ANDROID_NDK_HOME}")
    elseif (DEFINED ANDROID_NDK)
        set(ANDROID_NDK_HOME_VALUE "${ANDROID_NDK}")
    elseif (DEFINED CMAKE_ANDROID_NDK)
        set(ANDROID_NDK_HOME_VALUE "${CMAKE_ANDROID_NDK}")
    else()
        message(FATAL_ERROR "
        Please set an environment variable ANDROID_NDK_HOME
        For example:
        export ANDROID_NDK_HOME=/home/your-account/Android/Sdk/ndk-bundle
        Or:
        export ANDROID_NDK_HOME=/home/your-account/Android/android-ndk-r21b
        ")
    endif()

    if(WIN32)
        set(ANDROID_NDK_HOST_TAG "windows-x86_64")
    elseif(APPLE)
        set(ANDROID_NDK_HOST_TAG "darwin-x86_64")
    else()
        set(ANDROID_NDK_HOST_TAG "linux-x86_64")
    endif()

    set(ANDROID_NDK_LLVM_BIN "${ANDROID_NDK_HOME_VALUE}/toolchains/llvm/prebuilt/${ANDROID_NDK_HOST_TAG}/bin")
    set(CMAKE_C_COMPILER "${ANDROID_NDK_LLVM_BIN}/clang.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${ANDROID_NDK_LLVM_BIN}/clang++.exe" CACHE FILEPATH "" FORCE)

    #
    # 2. Check the presence of environment variable VCPKG_ROOT
    #
    if (DEFINED ENV{VCPKG_ROOT})
        set(VCPKG_ROOT_VALUE "$ENV{VCPKG_ROOT}")
    else()
        get_filename_component(VCPKG_ROOT_VALUE "${CMAKE_CURRENT_LIST_DIR}/../../vcpkg" ABSOLUTE)
        if (NOT EXISTS "${VCPKG_ROOT_VALUE}/scripts/buildsystems/vcpkg.cmake")
            get_filename_component(VCPKG_ROOT_VALUE "${CMAKE_CURRENT_LIST_DIR}/../../../../../../third_party/vcpkg" ABSOLUTE)
        endif()
        if (NOT EXISTS "${VCPKG_ROOT_VALUE}/scripts/buildsystems/vcpkg.cmake")
            message(FATAL_ERROR "
            Please set an environment variable VCPKG_ROOT
            For example:
            export VCPKG_ROOT=/path/to/vcpkg
            ")
        endif()
    endif()


    #
    # 3. Set VCPKG_TARGET_TRIPLET according to ANDROID_ABI
    # 
    # There are four different Android ABI, each of which maps to 
    # a vcpkg triplet. The following table outlines the mapping from vcpkg architectures to android architectures
    #
    # |VCPKG_TARGET_TRIPLET       | ANDROID_ABI          |
    # |---------------------------|----------------------|
    # |arm64-android              | arm64-v8a            |
    # |arm-android                | armeabi-v7a          |
    # |x64-android                | x86_64               |
    # |x86-android                | x86                  |
    #
    # The variable must be stored in the cache in order to successfully the two toolchains. 
    #
    if (ANDROID_ABI MATCHES "arm64-v8a")
        set(VCPKG_TARGET_TRIPLET "arm64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "armeabi-v7a")
        set(VCPKG_TARGET_TRIPLET "arm-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86_64")
        set(VCPKG_TARGET_TRIPLET "x64-android" CACHE STRING "" FORCE)
    elseif(ANDROID_ABI MATCHES "x86")
        set(VCPKG_TARGET_TRIPLET "x86-android" CACHE STRING "" FORCE)
    else()
        message(FATAL_ERROR "
        Please specify ANDROID_ABI
        For example
        cmake ... -DANDROID_ABI=armeabi-v7a

        Possible ABIs are: arm64-v8a, armeabi-v7a, x64-android, x86-android
        ")
    endif()
    message("vcpkg_android.cmake: VCPKG_TARGET_TRIPLET was set to ${VCPKG_TARGET_TRIPLET}")


    #
    # 4. Combine vcpkg and Android toolchains
    #

    # vcpkg and android both provide dedicated toolchains:
    #
    # vcpkg_toolchain_file=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
    # android_toolchain_file=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake
    #
    # When using vcpkg, the vcpkg toolchain shall be specified first. 
    # However, vcpkg provides a way to preload and additional toolchain, 
    # with the VCPKG_CHAINLOAD_TOOLCHAIN_FILE option.
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${ANDROID_NDK_HOME_VALUE}/build/cmake/android.toolchain.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT_VALUE}/scripts/buildsystems/vcpkg.cmake")
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        ANDROID_ABI
        ANDROID_NDK
        ANDROID_NDK_HOME
        ANDROID_PLATFORM
        CMAKE_ANDROID_NDK
        CMAKE_ANDROID_ARCH_ABI
        CMAKE_C_COMPILER
        CMAKE_CXX_COMPILER
        CMAKE_MAKE_PROGRAM
        CMAKE_SYSTEM_NAME
        CMAKE_SYSTEM_VERSION
        CMAKE_TOOLCHAIN_FILE
        OPENSSL_ROOT_DIR
        OPENSSL_USE_STATIC_LIBS
        VCPKG_CHAINLOAD_TOOLCHAIN_FILE
        VCPKG_TARGET_TRIPLET
        VCPKG_ROOT_VALUE
    )
    message("vcpkg_android.cmake: CMAKE_TOOLCHAIN_FILE was set to ${CMAKE_TOOLCHAIN_FILE}")
    message("vcpkg_android.cmake: VCPKG_CHAINLOAD_TOOLCHAIN_FILE was set to ${VCPKG_CHAINLOAD_TOOLCHAIN_FILE}")

endif(VCPKG_TARGET_ANDROID)
