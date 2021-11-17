source common.sh

needLocalStore "'--no-require-sigs' can’t be used with the daemon"

# We can produce drvs directly into the binary cache
clearStore
clearCacheCache
nix-instantiate --store "file://$cacheDir" dependencies.nix

# Create the binary cache.
clearStore
clearCache
outPath=$(nix-build dependencies.nix --no-out-link)

nix copy --to file://$cacheDir $outPath

# Test copying build logs to the binary cache.
nix log --store file://$cacheDir $outPath 2>&1 | grep 'is not available'
nix store copy-log --to file://$cacheDir $outPath
nix log --store file://$cacheDir $outPath | grep FOO
rm -rf $TEST_ROOT/var/log/nix
nix log $outPath 2>&1 | grep 'is not available'
nix log --substituters file://$cacheDir $outPath | grep FOO

# Test copying build logs from the binary cache.
nix store copy-log --from file://$cacheDir $(nix-store -qd $outPath)
nix log $outPath | grep FOO

basicDownloadTests() {
    # No uploading tests bcause upload with force HTTP doesn't work.

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
basicDownloadTests


# Test HttpBinaryCacheStore.
export _NIX_FORCE_HTTP=1
basicDownloadTests


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

rm -v $(grep -l "StorePath:.*dependencies-input-2" $cacheDir/*.narinfo)

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "copying path.*input-0" $TEST_ROOT/log
grep -q "copying path.*input-2" $TEST_ROOT/log
grep -q "copying path.*top" $TEST_ROOT/log


# Idem, but without cached .narinfo.
clearStore
clearCacheCache

nix-build --substituters "file://$cacheDir" --no-require-sigs dependencies.nix -o $TEST_ROOT/result 2>&1 | tee $TEST_ROOT/log
grep -q "don't know how to build" $TEST_ROOT/log
grep -q "building.*input-1" $TEST_ROOT/log
grep -q "building.*input-2" $TEST_ROOT/log
grep -q "copying path.*input-0" $TEST_ROOT/log
grep -q "copying path.*top" $TEST_ROOT/log


# Create a signed binary cache.
clearCache
clearCacheCache

nix key generate-secret --key-name test.nixos.org-1 > $TEST_ROOT/sk1
publicKey=$(nix key convert-secret-to-public < $TEST_ROOT/sk1)

nix key generate-secret --key-name test.nixos.org-1 > $TEST_ROOT/sk2
badKey=$(nix key convert-secret-to-public < $TEST_ROOT/sk2)

nix key generate-secret --key-name foo.nixos.org-1 > $TEST_ROOT/sk3
otherKey=$(nix key convert-secret-to-public < $TEST_ROOT/sk3)

_NIX_FORCE_HTTP= nix copy --to file://$cacheDir?secret-key=$TEST_ROOT/sk1 $outPath


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


unset _NIX_FORCE_HTTP


# Test 'nix verify --all' on a binary cache.
nix store verify -vvvvv --all --store file://$cacheDir --no-trust


# Test local NAR caching.
narCache=$TEST_ROOT/nar-cache
rm -rf $narCache
mkdir $narCache

[[ $(nix store cat --store "file://$cacheDir?local-nar-cache=$narCache" $outPath/foobar) = FOOBAR ]]

rm -rfv "$cacheDir/nar"

[[ $(nix store cat --store "file://$cacheDir?local-nar-cache=$narCache" $outPath/foobar) = FOOBAR ]]

(! nix store cat --store file://$cacheDir $outPath/foobar)


# Test NAR listing generation.
clearCache

outPath=$(nix-build --no-out-link -E '
  with import ./config.nix;
  mkDerivation {
    name = "nar-listing";
    buildCommand = "mkdir $out; echo foo > $out/bar; ln -s xyzzy $out/link";
  }
')

nix copy --to file://$cacheDir?write-nar-listing=1 $outPath

diff -u \
    <(jq -S < $cacheDir/$(basename $outPath | cut -c1-32).ls) \
    <(echo '{"version":1,"root":{"type":"directory","entries":{"bar":{"type":"regular","size":4,"narOffset":232},"link":{"type":"symlink","target":"xyzzy"}}}}' | jq -S)


# Test debug info index generation.
clearCache

outPath=$(nix-build --no-out-link -E '
  with import ./config.nix;
  mkDerivation {
    name = "debug-info";
    buildCommand = "mkdir -p $out/lib/debug/.build-id/02; echo foo > $out/lib/debug/.build-id/02/623eda209c26a59b1a8638ff7752f6b945c26b.debug";
  }
')

nix copy --to "file://$cacheDir?index-debug-info=1&compression=none" $outPath

diff -u \
    <(cat $cacheDir/debuginfo/02623eda209c26a59b1a8638ff7752f6b945c26b.debug | jq -S) \
    <(echo '{"archive":"../nar/100vxs724qr46phz8m24iswmg9p3785hsyagz0kchf6q6gf06sw6.nar","member":"lib/debug/.build-id/02/623eda209c26a59b1a8638ff7752f6b945c26b.debug"}' | jq -S)

# Test against issue https://github.com/NixOS/nix/issues/3964
#
expr='
  with import ./config.nix;
  mkDerivation {
    name = "multi-output";
    buildCommand = "mkdir -p $out; echo foo > $doc; echo $doc > $out/docref";
    outputs = ["out" "doc"];
  }
'
outPath=$(nix-build --no-out-link -E "$expr")
docPath=$(nix-store -q --references $outPath)

# $ nix-store -q --tree $outPath
# ...-multi-output
# +---...-multi-output-doc

nix copy --to "file://$cacheDir" $outPath

hashpart() {
  basename "$1" | cut -c1-32
}

# break the closure of out by removing doc
rm $cacheDir/$(hashpart $docPath).narinfo

nix-store --delete $outPath $docPath
# -vvv is the level that logs during the loop
timeout 60 nix-build --no-out-link -E "$expr" --option substituters "file://$cacheDir" \
  --option trusted-binary-caches "file://$cacheDir"  --no-require-sigs
