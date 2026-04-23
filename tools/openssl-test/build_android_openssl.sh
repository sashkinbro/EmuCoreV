#!/usr/bin/env bash
set -euo pipefail

export ANDROID_NDK_ROOT="/c/Users/sasha/AppData/Local/Android/Sdk/ndk/29.0.14206865"
export PATH="/usr/bin:$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"

cd /c/Users/sasha/AndroidStudioProjects/EmuCoreV/tools/openssl-test/openssl-3.3.2

perl Configure android-arm64 no-shared no-tests -D__ANDROID_API__=28 \
  --prefix=/c/Users/sasha/AndroidStudioProjects/EmuCoreV/tools/openssl-test/out \
  --openssldir=/c/Users/sasha/AndroidStudioProjects/EmuCoreV/tools/openssl-test/out

make -j4
make install_sw
