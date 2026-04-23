call .\bootstrap.bat
.\b2 tools\bcp

set SUBSETS_OUTDIR=.\subsets-boost

if exist "%SUBSETS_OUTDIR%" rmdir /s /q "%SUBSETS_OUTDIR%"
md "%SUBSETS_OUTDIR%"

for /d %%L in ("libs\*") do (
    if exist "%%L\doc" rmdir /s /q "%%L\doc"
    if exist "%%L\test" rmdir /s /q "%%L\test"
    if exist "%%L\example" rmdir /s /q "%%L\example"
)

.\dist\bin\bcp ^
    algorithm ^
    concept_check ^
    config ^
    container_hash ^
    crc ^
    describe ^
    detail ^
    filesystem ^
    icl ^
    log ^
    mp11 ^
    optional ^
    predef ^
    program_options ^
    range ^
    static_assert ^
    system ^
    unordered ^
    variant2 ^
    bootstrap.bat ^
    bootstrap.sh ^
    boostcpp.jam ^
    boost-build.jam ^
    tools/build ^
    tools/boost_install/BoostConfig.cmake ^
    tools/boost_install/BoostDetectToolset.cmake ^
    tools/boost_install/boost-install.jam ^
    tools/boost_install/BoostDetectToolset.cmake ^
    tools/boost_install/boost-install-dirs.jam ^
    %SUBSETS_OUTDIR%