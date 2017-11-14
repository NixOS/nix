source common.sh

clearStore
clearCache

nix-store --generate-binary-cache-key cache1.example.org $TEST_ROOT/sk1 $TEST_ROOT/pk1
pk1=$(cat $TEST_ROOT/pk1)
nix-store --generate-binary-cache-key cache2.example.org $TEST_ROOT/sk2 $TEST_ROOT/pk2
pk2=$(cat $TEST_ROOT/pk2)

# Build a path.
outPath=$(nix-build dependencies.nix --no-out-link --secret-key-files "$TEST_ROOT/sk1 $TEST_ROOT/sk2")

# Verify that the path got signed.
info=$(nix path-info --json $outPath)
[[ $info =~ '"ultimate":true' ]]
[[ $info =~ 'cache1.example.org' ]]
[[ $info =~ 'cache2.example.org' ]]

# Test "nix verify".
nix verify -r $outPath

expect 2 nix verify -r $outPath --sigs-needed 1

nix verify -r $outPath --sigs-needed 1 --binary-cache-public-keys $pk1

expect 2 nix verify -r $outPath --sigs-needed 2 --binary-cache-public-keys $pk1

nix verify -r $outPath --sigs-needed 2 --binary-cache-public-keys "$pk1 $pk2"

nix verify --all --sigs-needed 2 --binary-cache-public-keys "$pk1 $pk2"

# Build something unsigned.
outPath2=$(nix-build simple.nix --no-out-link)

nix verify -r $outPath

# Verify that the path did not get signed but does have the ultimate bit.
info=$(nix path-info --json $outPath2)
[[ $info =~ '"ultimate":true' ]]
(! [[ $info =~ 'signatures' ]])

# Test "nix verify".
nix verify -r $outPath2

expect 2 nix verify -r $outPath2 --sigs-needed 1

expect 2 nix verify -r $outPath2 --sigs-needed 1 --binary-cache-public-keys $pk1

# Test "nix sign-paths".
nix sign-paths --key-file $TEST_ROOT/sk1 $outPath2

nix verify -r $outPath2 --sigs-needed 1 --binary-cache-public-keys $pk1

# Copy to a binary cache.
nix copy --to file://$cacheDir $outPath2

# Verify that signatures got copied.
info=$(nix path-info --store file://$cacheDir --json $outPath2)
(! [[ $info =~ '"ultimate":true' ]])
[[ $info =~ 'cache1.example.org' ]]
(! [[ $info =~ 'cache2.example.org' ]])
