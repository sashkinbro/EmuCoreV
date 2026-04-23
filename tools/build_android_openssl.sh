#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENSSL_VERSION="${OPENSSL_VERSION:-openssl-3.3.2}"
OPENSSL_SOURCE_DIR="$PROJECT_ROOT/tools/openssl-test/$OPENSSL_VERSION"
OPENSSL_OUT_DIR="$PROJECT_ROOT/tools/openssl-test/out"

if [[ -n "${ANDROID_NDK_ROOT:-}" ]]; then
  NDK_DIR="$ANDROID_NDK_ROOT"
elif [[ -n "${ANDROID_NDK_HOME:-}" ]]; then
  NDK_DIR="$ANDROID_NDK_HOME"
else
  NDK_DIR="$HOME/AppData/Local/Android/Sdk/ndk/29.0.14206865"
fi

if [[ ! -d "$NDK_DIR" ]]; then
  echo "ANDROID_NDK_ROOT/ANDROID_NDK_HOME is not set to a valid NDK path." >&2
  echo "Current value: $NDK_DIR" >&2
  exit 1
fi

if [[ ! -d "$OPENSSL_SOURCE_DIR" ]]; then
  echo "OpenSSL source directory not found: $OPENSSL_SOURCE_DIR" >&2
  exit 1
fi

export ANDROID_NDK_ROOT="$NDK_DIR"
export PATH="/usr/bin:$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"

cd "$OPENSSL_SOURCE_DIR"

perl Configure android-arm64 no-shared no-tests -D__ANDROID_API__=28 \
  --prefix="$OPENSSL_OUT_DIR" \
  --openssldir="$OPENSSL_OUT_DIR"

make -j4
make install_sw
