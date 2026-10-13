$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vendor = Join-Path $repoRoot 'app/src/main/cpp/vita3k'
$outputDir = Join-Path $repoRoot 'app/build/logging-regressions'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$includes = @('vita3k/util/include', 'external/boost', 'external/fmt/include', 'external/spdlog/include')
$includeArgs = $includes | ForEach-Object { '-I' + (Join-Path $vendor $_) }
foreach ($mode in @('debug', 'release')) {
    [string[]] $modeDefine = if ($mode -eq 'release') { @('-DNDEBUG') } else { @() }
    $binary = Join-Path $outputDir ('logging_regressions_' + $mode + '.exe')
    & g++ -std=c++20 -DFMT_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL -DBOOST_ALL_NO_LIB @modeDefine @includeArgs (Join-Path $PSScriptRoot 'tests/logging_regressions.cpp') -o $binary
    if ($LASTEXITCODE -ne 0) { throw "Logging regression build failed: $mode" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "Logging regression checks failed: $mode" }
}
