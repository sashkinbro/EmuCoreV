param(
    [Parameter(Mandatory = $true)][string]$Serial,
    [string]$Sdk = (Join-Path $env:LOCALAPPDATA 'Android/Sdk'),
    [string]$NativeDirectory = '',
    [string]$CoreLibrary = '',
    [string]$Filter = '*'
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$adb = Join-Path $Sdk 'platform-tools/adb.exe'
if (!$NativeDirectory) {
    $candidates = @(Get-ChildItem (Join-Path $repoRoot 'app/build/intermediates/cxx/Debug') -Filter emucorev-core-tests -Recurse)
    if ($candidates.Count -ne 1) { throw 'Specify NativeDirectory containing emucorev-core-tests and libVita3K.so' }
    $NativeDirectory = $candidates[0].DirectoryName
}
if (!$CoreLibrary) { $CoreLibrary = Join-Path $NativeDirectory 'libVita3K.so' }
if ($Filter -notmatch '^[A-Za-z0-9_.*:?-]+$') { throw 'Invalid GoogleTest filter' }

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}

# Run separately from installed apps, with an explicit serial and a unique directory.
$remote = '/data/local/tmp/emucorev-core-tests-' + [Guid]::NewGuid().ToString('N')
Invoke-Checked $adb @('-s', $Serial, 'shell', 'mkdir', '-p', $remote)
try {
    Invoke-Checked $adb @('-s', $Serial, 'push', (Join-Path $NativeDirectory 'emucorev-core-tests'), "$remote/tests")
    Invoke-Checked $adb @('-s', $Serial, 'push', $CoreLibrary, "$remote/libVita3K.so")
    Invoke-Checked $adb @('-s', $Serial, 'shell', 'chmod', '700', "$remote/tests")
    Invoke-Checked $adb @('-s', $Serial, 'shell', "LD_LIBRARY_PATH=$remote TMPDIR=$remote $remote/tests --gtest_filter=$Filter")
} finally {
    & $adb -s $Serial shell rm -f "$remote/tests" "$remote/libVita3K.so"
    & $adb -s $Serial shell rmdir $remote
}
