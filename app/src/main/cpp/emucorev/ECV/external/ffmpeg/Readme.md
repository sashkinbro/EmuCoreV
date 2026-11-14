## FFmpeg builtins

This repository contains the ffmpeg builtins for Vita3K and the CI to build them using vcpkg and github action.

The Windows version is built using clang-cl, this is done so that inline assembly optimisations (which are not supported by MSVC) can be enabled.
