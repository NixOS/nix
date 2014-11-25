source common.sh

clearStore
clearManifests
clearCache

# Create the binary cache.
outPath=$(nix-build dependencies.nix --no-out-link)

nix-push --dest $cacheDir $outPath


# By default, a binary cache doesn't support "nix-env -qas", but does
# support installation.
clearStore
rm -f $NIX_STATE_DIR/binary-cache*

export _NIX_CACHE_FILE_URLS=1

nix-env --option binary-caches "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "---"

nix-store --option binary-caches "file://$cacheDir" -r $outPath

[ -x $outPath/program ]


# But with the right configuration, "nix-env -qas" should also work.
clearStore
rm -f $NIX_STATE_DIR/binary-cache*
echo "WantMassQuery: 1" >> $cacheDir/nix-cache-info

nix-env --option binary-caches "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "--S"

x=$(nix-env -f dependencies.nix -qas \* --prebuilt-only)
[ -z "$x" ]

nix-store --option binary-caches "file://$cacheDir" -r $outPath

nix-store --check-validity $outPath
nix-store -qR $outPath | grep input-2


# Test whether Nix notices if the NAR doesn't match the hash in the NAR info.
clearStore

nar=$(ls $cacheDir/*.nar.xz | head -n1)
mv $nar $nar.good
mkdir -p $TEST_ROOT/empty
nix-store --dump $TEST_ROOT/empty | xz > $nar

nix-build --option binary-caches "file://$cacheDir" dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "hash mismatch in downloaded path" $TEST_ROOT/log

mv $nar.good $nar


# Test whether this unsigned cache is rejected if the user requires signed caches.
clearStore

rm -f $NIX_STATE_DIR/binary-cache*

if nix-store --option binary-caches "file://$cacheDir" --option signed-binary-caches '*' -r $outPath; then
    echo "unsigned binary cache incorrectly accepted"
    exit 1
fi


# Test whether fallback works if we have cached info but the
# corresponding NAR has disappeared.
clearStore

nix-build --option binary-caches "file://$cacheDir" dependencies.nix --dry-run # get info

mkdir $cacheDir/tmp
mv $cacheDir/*.nar* $cacheDir/tmp/

NIX_DEBUG_SUBST=1 nix-build --option binary-caches "file://$cacheDir" dependencies.nix -o $TEST_ROOT/result --fallback

mv $cacheDir/tmp/* $cacheDir/


# Test whether building works if the binary cache contains an
# incomplete closure.
clearStore

rm $(grep -l "StorePath:.*dependencies-input-2" $cacheDir/*.narinfo)

nix-build --option binary-caches "file://$cacheDir" dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "Downloading" $TEST_ROOT/log
