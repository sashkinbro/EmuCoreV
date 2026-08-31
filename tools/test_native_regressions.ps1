$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vendor = Join-Path $repoRoot 'app/src/main/cpp/vita3k'
$outputDir = Join-Path $repoRoot 'app/build/native-regressions'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools are required.' }
$buildTools = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$buildTools) { throw 'Visual Studio C++ toolchain was not found.' }
$vcvars = Join-Path $buildTools 'VC/Auxiliary/Build/vcvars64.bat'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$includes = @('vita3k/config/include', 'vita3k/packages/include', 'vita3k/ime/include', 'vita3k/mem/include', 'vita3k/io/include', 'vita3k/util/include', 'external/boost', 'external/fmt/include', 'external/spdlog/include')
$includeArgs = ($includes | ForEach-Object { '/I"' + (Join-Path $vendor $_) + '"' }) -join ' '
$testSource = Join-Path $PSScriptRoot 'tests/native_core_regressions.cpp'
$sfoSource = Join-Path $vendor 'vita3k/packages/src/sfo.cpp'
$imeSource = Join-Path $vendor 'vita3k/ime/src/ime.cpp'
$compileCommand = 'call "' + $vcvars + '" >nul && cl.exe /nologo /std:c++20 /EHsc /utf-8 /Zc:__cplusplus /DFMT_HEADER_ONLY /DSPDLOG_FMT_EXTERNAL /DBOOST_ALL_NO_LIB ' + $includeArgs + ' "' + $testSource + '" "' + $sfoSource + '" "' + $imeSource + '" /Fe:native_core_regressions.exe'
Push-Location $outputDir
try {
    & cmd.exe /d /s /c $compileCommand
    if ($LASTEXITCODE -ne 0) { throw "Native regression build failed: $LASTEXITCODE" }
    & (Join-Path $outputDir 'native_core_regressions.exe')
    if ($LASTEXITCODE -ne 0) { throw "Native regression checks failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
