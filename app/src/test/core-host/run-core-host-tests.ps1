# Builds and runs the EmuCoreV core host tests with llvm-mingw (clang++, C++20) via CMake + Ninja.
# No device or emulator build is involved. Override tool locations with the parameters.
param(
    [string]$Mingw = "C:\Users\SBRO\tools\llvm-mingw\bin",
    [string]$CMakeBin = "C:\Users\SBRO\AppData\Local\Android\Sdk\cmake\3.30.5\bin",
    [string]$BuildDir = (Join-Path $env:TEMP "emucorev-core-host-build"),
    [string]$Filter = "*"   # gtest filter, e.g. "GuestAtomic*:ClibChk*:RWLock*"
)
# Native tools write warnings to stderr; exit codes are checked explicitly below.
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH = "$Mingw;$CMakeBin;$env:PATH"
& "$CMakeBin\cmake.exe" -S $here -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_MAKE_PROGRAM="$CMakeBin\ninja.exe" `
    -DCMAKE_C_COMPILER="$Mingw\x86_64-w64-mingw32-clang.exe" `
    -DCMAKE_CXX_COMPILER="$Mingw\x86_64-w64-mingw32-clang++.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$CMakeBin\cmake.exe" --build $BuildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& "$BuildDir\core_host_tests.exe" "--gtest_filter=$Filter"
exit $LASTEXITCODE
