#!/usr/bin/env bash

source common.sh

runTests() {

clearStoreIfPossible
clearCache

keyType="$1"

nix key generate-secret --key-name cache1.example.org --key-type "$keyType" > "$TEST_ROOT"/sk1
nix key convert-secret-to-public < "$TEST_ROOT"/sk1 > "$TEST_ROOT"/pk1
pk1=$(cat "$TEST_ROOT"/pk1)
nix key generate-secret --key-name cache2.example.org --key-type "$keyType" > "$TEST_ROOT"/sk2
nix key convert-secret-to-public < "$TEST_ROOT"/sk2 > "$TEST_ROOT"/pk2
pk2=$(cat "$TEST_ROOT"/pk2)

# Test PEM conversion.
if [[ "$keyType" == "ed25519" ]]; then
    # Ed25519 keys cannot be converted to PEM.
    expectStderr 1 nix key convert-secret-to-pem < "$TEST_ROOT"/sk1 | grepQuiet "conversion to PEM is not implemented for this key type"
    expectStderr 1 nix key convert-public-to-pem < "$TEST_ROOT"/pk1 | grepQuiet "Ed25519 public keys cannot be converted to PEM"
else
    # ML-DSA-* keys can be converted to PEM.
    nix key convert-secret-to-pem < "$TEST_ROOT"/sk1 > "$TEST_ROOT"/sk1.pem
    grepQuiet "^-----BEGIN PRIVATE KEY-----$" "$TEST_ROOT"/sk1.pem
    grepQuiet "^-----END PRIVATE KEY-----$" "$TEST_ROOT"/sk1.pem

    nix key convert-public-to-pem < "$TEST_ROOT"/pk1 > "$TEST_ROOT"/pk1.pem
    grepQuiet "^-----BEGIN PUBLIC KEY-----$" "$TEST_ROOT"/pk1.pem
    grepQuiet "^-----END PUBLIC KEY-----$" "$TEST_ROOT"/pk1.pem

    # If openssl is available, verify that it can parse the PEM keys.
    if type -p openssl > /dev/null; then
        openssl pkey -text -noout < "$TEST_ROOT"/sk1.pem
        openssl pkey -pubin -text -noout < "$TEST_ROOT"/pk1.pem
    fi

    # Feeding a secret key to convert-public-to-pem (or vice versa) should fail.
    expect 1 nix key convert-public-to-pem < "$TEST_ROOT"/sk1
    expect 1 nix key convert-secret-to-pem < "$TEST_ROOT"/pk1
fi

# Build a path.
outPath=$(nix-build dependencies.nix --no-out-link --secret-key-files "$TEST_ROOT/sk1 $TEST_ROOT/sk2")

# Verify that the path got signed.
info=$(nix path-info --json --json-format 2 "$outPath")
echo "$info" | jq -e '.info.[] | .ultimate == true'
TODO_NixOS # looks like an actual bug? Following line fails on NixOS:
echo "$info" | jq -e '.info.[] | .signatures.[] | select(startswith("cache1.example.org"))'
echo "$info" | jq -e '.info.[] | .signatures.[] | select(startswith("cache2.example.org"))'

# Test "nix store verify".
nix store verify -r "$outPath"

expect 2 nix store verify -r "$outPath" --sigs-needed 1

nix store verify -r "$outPath" --sigs-needed 1 --trusted-public-keys "$pk1"

expect 2 nix store verify -r "$outPath" --sigs-needed 2 --trusted-public-keys "$pk1"

nix store verify -r "$outPath" --sigs-needed 2 --trusted-public-keys "$pk1 $pk2"

nix store verify --all --sigs-needed 2 --trusted-public-keys "$pk1 $pk2"

# Build something unsigned.
outPath2=$(nix-build simple.nix --no-out-link)

nix store verify -r "$outPath"

# Verify that the path did not get signed but does have the ultimate bit.
info=$(nix path-info --json --json-format 2 "$outPath2")
echo "$info" | jq -e '.info.[] | .ultimate == true'
echo "$info" | jq -e '.info.[] | .signatures == []'

# Test "nix store verify".
nix store verify -r "$outPath2"

expect 2 nix store verify -r "$outPath2" --sigs-needed 1

expect 2 nix store verify -r "$outPath2" --sigs-needed 1 --trusted-public-keys "$pk1"

# Test "nix store sign".
nix store sign --key-file "$TEST_ROOT"/sk1 "$outPath2"

nix store verify -r "$outPath2" --sigs-needed 1 --trusted-public-keys "$pk1"

# Build something content-addressed.
outPathCA=$(IMPURE_VAR1=foo IMPURE_VAR2=bar nix-build ./fixed.nix -A good.0 --no-out-link)

nix path-info --json --json-format 2 "$outPathCA" | jq -e '.info.[].ca | .method == "flat" and (.hash | startswith("md5-"))'

# Content-addressed paths don't need signatures, so they verify
# regardless of --sigs-needed.
nix store verify "$outPathCA"
nix store verify "$outPathCA" --sigs-needed 1000

# Check that signing a content-addressed path doesn't overflow validSigs
nix store sign --key-file "$TEST_ROOT"/sk1 "$outPathCA"
nix store verify -r "$outPathCA" --sigs-needed 1000 --trusted-public-keys "$pk1"

# Copy to a binary cache.
nix copy --to file://"$cacheDir" "$outPath2"

# Verify that signatures got copied.
info=$(nix path-info --store file://"$cacheDir" --json --json-format 2 "$outPath2")
echo "$info" | jq -e '.info.[] | .ultimate == false'
echo "$info" | jq -e '.info.[] | .signatures.[] | select(startswith("cache1.example.org"))'
echo "$info" | expect 4 jq -e '.info.[] | .signatures.[] | select(startswith("cache2.example.org"))'

# Verify that adding a signature to a path in a binary cache works.
nix store sign --store file://"$cacheDir" --key-file "$TEST_ROOT"/sk2 "$outPath2"
info=$(nix path-info --store file://"$cacheDir" --json --json-format 2 "$outPath2")
echo "$info" | jq -e '.info.[] | .signatures.[] | select(startswith("cache1.example.org"))'
echo "$info" | jq -e '.info.[] | .signatures.[] | select(startswith("cache2.example.org"))'

# Copying to a diverted store should fail due to a lack of signatures by trusted keys.
chmod -R u+w "$TEST_ROOT"/store0 || true
rm -rf "$TEST_ROOT"/store0

# Fails or very flaky only on GHA + macOS:
#     expectStderr 1 nix copy --to $TEST_ROOT/store0 $outPath | grepQuiet -E 'cannot add path .* because it lacks a signature by a trusted key'
# but this works:
(! nix copy --to "$TEST_ROOT"/store0 "$outPath")

# But succeed if we supply the public keys.
nix copy --to "$TEST_ROOT"/store0 "$outPath" --trusted-public-keys "$pk1"

expect 2 nix store verify --store "$TEST_ROOT"/store0 -r "$outPath"

nix store verify --store "$TEST_ROOT"/store0 -r "$outPath" --trusted-public-keys "$pk1"
nix store verify --store "$TEST_ROOT"/store0 -r "$outPath" --sigs-needed 2 --trusted-public-keys "$pk1 $pk2"

# It should also succeed if we disable signature checking.
(! nix copy --to "$TEST_ROOT"/store0 "$outPath2")
nix copy --to "$TEST_ROOT"/store0?require-sigs=false "$outPath2"

# But signatures should still get copied.
nix store verify --store "$TEST_ROOT"/store0 -r "$outPath2" --trusted-public-keys "$pk1"

# Content-addressed stuff can be copied without signatures.
nix copy --to "$TEST_ROOT"/store0 "$outPathCA"

# Test multiple signing keys
nix copy --to "file://$TEST_ROOT/storemultisig?secret-keys=$TEST_ROOT/sk1,$TEST_ROOT/sk2" "$outPath"
for file in "$TEST_ROOT/storemultisig/"*.narinfo; do
    if [[ "$(grep -cE  '^Sig: cache[1,2]\.example.org' "$file")" -ne 2 ]]; then
        echo "ERROR: Cannot find cache1.example.org and cache2.example.org signatures in ${file}"
        cat "${file}"
        exit 1
    fi
done

}

runTests ed25519
runTests ml-dsa-44
runTests ml-dsa-65
runTests ml-dsa-87
