source common.sh

clearStore
clearCache

# Create the binary cache.
outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to file://$cacheDir $outPath


basicTests() {

    # By default, a binary cache doesn't support "nix-env -qas", but does
    # support installation.
    clearStore
    clearCacheCache

    nix-env --substituters "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "---"

    nix-store --substituters "file://$cacheDir" --no-require-sigs -r $outPath

    [ -x $outPath/program ]


    # But with the right configuration, "nix-env -qas" should also work.
    clearStore
    clearCacheCache
    echo "WantMassQuery: 1" >> $cacheDir/nix-cache-info

    nix-env --substituters "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "--S"
    nix-env --substituters "file://$cacheDir" -f dependencies.nix -qas \* | grep -- "--S"

    x=$(nix-env -f dependencies.nix -qas \* --prebuilt-only)
    [ -z "$x" ]

    nix-store --substituters "file://$cacheDir" --no-require-sigs -r $outPath

    nix-store --check-validity $outPath
    nix-store -qR $outPath | grep input-2

    echo "WantMassQuery: 0" >> $cacheDir/nix-cache-info
}


# Test LocalBinaryCacheStore.
basicTests


# Test HttpBinaryCacheStore.
export _NIX_FORCE_HTTP_BINARY_CACHE_STORE=1
basicTests


# Test whether Nix notices if the NAR doesn't match the hash in the NAR info.
clearStore

nar=$(ls $cacheDir/nar/*.nar.xz | head -n1)
mv $nar $nar.good
mkdir -p $TEST_ROOT/empty
nix-store --dump $TEST_ROOT/empty | xz > $nar

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "hash mismatch" $TEST_ROOT/log

mv $nar.good $nar


# Test whether this unsigned cache is rejected if the user requires signed caches.
clearStore
clearCacheCache

if nix-store --substituters "file://$cacheDir" -r $outPath; then
    echo "unsigned binary cache incorrectly accepted"
    exit 1
fi


# Test whether fallback works if a NAR has disappeared. This does not require --fallback.
clearStore

mv $cacheDir/nar $cacheDir/nar2

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result

mv $cacheDir/nar2 $cacheDir/nar


# Test whether fallback works if a NAR is corrupted. This does require --fallback.
clearStore

mv $cacheDir/nar $cacheDir/nar2
mkdir $cacheDir/nar
for i in $(cd $cacheDir/nar2 && echo *); do touch $cacheDir/nar/$i; done

(! nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result)

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result --fallback

rm -rf $cacheDir/nar
mv $cacheDir/nar2 $cacheDir/nar


# Test whether building works if the binary cache contains an
# incomplete closure.
clearStore

rm $(grep -l "StorePath:.*dependencies-input-2" $cacheDir/*.narinfo)

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "copying path" $TEST_ROOT/log


if [ -n "$HAVE_SODIUM" ]; then

# Create a signed binary cache.
clearCache
clearCacheCache

declare -a res=($(nix-store --generate-binary-cache-key test.nixos.org-1 $TEST_ROOT/sk1 $TEST_ROOT/pk1 ))
publicKey="$(cat $TEST_ROOT/pk1)"

res=($(nix-store --generate-binary-cache-key test.nixos.org-1 $TEST_ROOT/sk2 $TEST_ROOT/pk2))
badKey="$(cat $TEST_ROOT/pk2)"

res=($(nix-store --generate-binary-cache-key foo.nixos.org-1 $TEST_ROOT/sk3 $TEST_ROOT/pk3))
otherKey="$(cat $TEST_ROOT/pk3)"

_NIX_FORCE_HTTP_BINARY_CACHE_STORE= nix copy --to file://$cacheDir?secret-key=$TEST_ROOT/sk1 $outPath


# Downloading should fail if we don't provide a key.
clearStore
clearCacheCache

(! nix-store -r $outPath --substituters "file://$cacheDir")


# And it should fail if we provide an incorrect key.
clearStore
clearCacheCache

(! nix-store -r $outPath --substituters "file://$cacheDir" --trusted-public-keys "$badKey")


# It should succeed if we provide the correct key.
nix-store -r $outPath --substituters "file://$cacheDir" --trusted-public-keys "$otherKey $publicKey"


# It should fail if we corrupt the .narinfo.
clearStore

cacheDir2=$TEST_ROOT/binary-cache-2
rm -rf $cacheDir2
cp -r $cacheDir $cacheDir2

for i in $cacheDir2/*.narinfo; do
    grep -v References $i > $i.tmp
    mv $i.tmp $i
done

clearCacheCache

(! nix-store -r $outPath --substituters "file://$cacheDir2" --trusted-public-keys "$publicKey")

# If we provide a bad and a good binary cache, it should succeed.

nix-store -r $outPath --substituters "file://$cacheDir2 file://$cacheDir" --trusted-public-keys "$publicKey"

fi # HAVE_LIBSODIUM
