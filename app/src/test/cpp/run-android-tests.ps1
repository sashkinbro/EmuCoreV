param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [string]$Sdk = (Join-Path $env:LOCALAPPDATA 'Android/Sdk'),
    [string]$Filter = '*'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$buildDirectory = Join-Path $repoRoot 'app/build/native-tests'
$cmake = Join-Path $Sdk 'cmake/3.30.5/bin/cmake.exe'
$ninja = Join-Path $Sdk 'cmake/3.30.5/bin/ninja.exe'
$toolchain = Join-Path $Sdk 'ndk/29.0.14206865/build/cmake/android.toolchain.cmake'
$adb = Join-Path $Sdk 'platform-tools/adb.exe'

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}

Invoke-Checked $cmake @('-S', $PSScriptRoot, '-B', $buildDirectory, '-G', 'Ninja',
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain", "-DCMAKE_MAKE_PROGRAM=$ninja",
    '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=28', '-DANDROID_STL=c++_static',
    '-DCMAKE_BUILD_TYPE=Debug')
Invoke-Checked $cmake @('--build', $buildDirectory, '-j', '4')

# A unique shell-only directory avoids installing an app or touching another
# agent's running game, app storage, or native-test temporary files.
$remote = '/data/local/tmp/emucorev-tests-' + [Guid]::NewGuid().ToString('N')
Invoke-Checked $adb @('-s', $Serial, 'shell', 'mkdir', '-p', $remote)
try {
    Invoke-Checked $adb @('-s', $Serial, 'push', (Join-Path $buildDirectory 'cheat-tests'), "$remote/cheat-tests")
    Invoke-Checked $adb @('-s', $Serial, 'shell', 'chmod', '700', "$remote/cheat-tests")
    if ($Filter -notmatch '^[A-Za-z0-9_.*:?-]+$') { throw 'Invalid GoogleTest filter' }
    Invoke-Checked $adb @('-s', $Serial, 'shell', "TMPDIR=$remote $remote/cheat-tests --gtest_brief=1 --gtest_filter=$Filter")
} finally {
    & $adb -s $Serial shell rm -f "$remote/cheat-tests"
    & $adb -s $Serial shell rmdir $remote
}
