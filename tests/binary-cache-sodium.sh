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

# Create a signed binary cache.
outPath=$(nix-build $NIX_TEST_ROOT/dependencies.nix --no-out-link)

clearCache

declare -a res=($(nix-store --generate-binary-cache-key test.nixos.org-1 $TEST_ROOT/sk1 $TEST_ROOT/pk1 ))
publicKey="$(cat $TEST_ROOT/pk1)"

res=($(nix-store --generate-binary-cache-key test.nixos.org-1 $TEST_ROOT/sk2 $TEST_ROOT/pk2))
badKey="$(cat $TEST_ROOT/pk2)"

res=($(nix-store --generate-binary-cache-key foo.nixos.org-1 $TEST_ROOT/sk3 $TEST_ROOT/pk3))
otherKey="$(cat $TEST_ROOT/pk3)"

nix copy --recursive --to file://$cacheDir?secret-key=$TEST_ROOT/sk1 $outPath


# Downloading should fail if we don't provide a key.
clearStore
clearCacheCache

(! nix-store -r $outPath --option binary-caches "file://$cacheDir" --option signed-binary-caches '*' )


# And it should fail if we provide an incorrect key.
clearStore
clearCacheCache

(! nix-store -r $outPath --option binary-caches "file://$cacheDir" --option signed-binary-caches '*' --option binary-cache-public-keys "$badKey")


# It should succeed if we provide the correct key.
nix-store -r $outPath --option binary-caches "file://$cacheDir" --option signed-binary-caches '*' --option binary-cache-public-keys "$otherKey $publicKey"


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

(! nix-store -r $outPath --option binary-caches "file://$cacheDir2" --option signed-binary-caches '*' --option binary-cache-public-keys "$publicKey")

# If we provide a bad and a good binary cache, it should succeed.

nix-store -r $outPath --option binary-caches "file://$cacheDir2 file://$cacheDir" --option signed-binary-caches '*' --option binary-cache-public-keys "$publicKey"
