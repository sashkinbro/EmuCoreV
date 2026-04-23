./bootstrap.sh
./b2 tools/bcp

SUBSETS_OUTDIR=./subsets-boost

if [ -f "$SUBSETS_OUTDIR" ]; then
    rm "$SUBSETS_OUTDIR"
elif [ -d "$SUBSETS_OUTDIR" ]; then
    rm -r "$SUBSETS_OUTDIR"
fi

echo "Creating $SUBSETS_OUTDIR"
mkdir -p "$SUBSETS_OUTDIR"

for L in libs/*; do
    [ -d "$L/doc" ] && rm -rf "$L/doc"
    [ -d "$L/test" ] && rm -rf "$L/test"
    [ -d "$L/example" ] && rm -rf "$L/example"
done

./dist/bin/bcp \
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
system ^
unordered ^
variant2 ^
bootstrap.bat \
bootstrap.sh \
boostcpp.jam \
boost-build.jam \
tools/build \
tools/boost_install/BoostConfig.cmake \
tools/boost_install/BoostDetectToolset.cmake \
tools/boost_install/boost-install.jam \
tools/boost_install/boost-install-dirs.jam \
"$SUBSETS_OUTDIR"