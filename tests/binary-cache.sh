export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

cacheDir=$TEST_ROOT/binary-cache

clearCache() {
    rm -rf "$cacheDir"
}

clearCacheCache() {
    rm -f $TEST_HOME/.cache/nix/binary-cache*
}

clearStore
clearCache

# Create the binary cache.
outPath=$(nix-build $NIX_TEST_ROOT/dependencies.nix --no-out-link)

nix copy --recursive --to file://$cacheDir $outPath


basicTests() {

    # By default, a binary cache doesn't support "nix-env -qas", but does
    # support installation.
    clearStore
    clearCacheCache

    nix-env --option binary-caches "file://$cacheDir" -f $NIX_TEST_ROOT/dependencies.nix -qas \* | grep -- "---"

    nix-store --option binary-caches "file://$cacheDir" --option signed-binary-caches '' -r $outPath

    [ -x $outPath/program ]


    # But with the right configuration, "nix-env -qas" should also work.
    clearStore
    clearCacheCache
    echo "WantMassQuery: 1" >> $cacheDir/nix-cache-info

    nix-env --option binary-caches "file://$cacheDir" -f $NIX_TEST_ROOT/dependencies.nix -qas \* | grep -- "--S"
    nix-env --option binary-caches "file://$cacheDir" -f $NIX_TEST_ROOT/dependencies.nix -qas \* | grep -- "--S"

    x=$(nix-env -f $NIX_TEST_ROOT/dependencies.nix -qas \* --prebuilt-only)
    [ -z "$x" ]

    nix-store --option binary-caches "file://$cacheDir" --option signed-binary-caches '' -r $outPath

    nix-store --check-validity $outPath
    nix-store -qR $outPath | grep input-2

    echo "WantMassQuery: 0" >> $cacheDir/nix-cache-info
}


# Test LocalBinaryCacheStore.
basicTests


# Test HttpBinaryCacheStore.
export _NIX_FORCE_HTTP_BINARY_CACHE_STORE=1
basicTests


unset _NIX_FORCE_HTTP_BINARY_CACHE_STORE


# Test whether Nix notices if the NAR doesn't match the hash in the NAR info.
clearStore

nar=$(ls $cacheDir/nar/*.nar.xz | head -n1)
mv $nar $nar.good
mkdir -p $TEST_ROOT/empty
nix-store --dump $TEST_ROOT/empty | xz > $nar

nix-build --option binary-caches "file://$cacheDir" --option signed-binary-caches '' $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "hash mismatch" $TEST_ROOT/log

mv $nar.good $nar


# Test whether this unsigned cache is rejected if the user requires signed caches.
clearStore
clearCacheCache

if nix-store --option binary-caches "file://$cacheDir" -r $outPath; then
    echo "unsigned binary cache incorrectly accepted"
    exit 1
fi


# Test whether fallback works if we have cached info but the
# corresponding NAR has disappeared.
clearStore

nix-build --option binary-caches "file://$cacheDir" $NIX_TEST_ROOT/dependencies.nix --dry-run # get info

mkdir $cacheDir/tmp
mv $cacheDir/*.nar* $cacheDir/tmp/

NIX_DEBUG_SUBST=1 nix-build --option binary-caches "file://$cacheDir" $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result --fallback

mv $cacheDir/tmp/* $cacheDir/


# Test whether building works if the binary cache contains an
# incomplete closure.
clearStore

rm $(grep -l "StorePath:.*dependencies-input-2" $cacheDir/*.narinfo)

nix-build --option binary-caches "file://$cacheDir" --option signed-binary-caches '' $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "fetching path" $TEST_ROOT/log
