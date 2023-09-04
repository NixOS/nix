source common.sh

clearStore

# Test fetching a flat file.
hash=$(nix-hash --flat --type sha256 ./fetchurl.sh)

outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr sha256 $hash --no-out-link)

cmp $outPath fetchurl.sh

# Do not re-fetch paths already present.
outPath2=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file:///does-not-exist/must-remain-unused/fetchurl.sh --argstr sha256 $hash --no-out-link)
test "$outPath" == "$outPath2"

# Now using a base-64 hash.
clearStore

hash=$(nix hash file --type sha512 --base64 ./fetchurl.sh)

outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr sha512 $hash --no-out-link)

cmp $outPath fetchurl.sh

# Now using an SRI hash.
clearStore

hash=$(nix hash file ./fetchurl.sh)

[[ $hash =~ ^sha256- ]]

outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr hash $hash --no-out-link)

cmp $outPath fetchurl.sh

# Test that we can substitute from a different store dir.
clearStore

other_store=file://$TEST_ROOT/other_store?store=/fnord/store

hash=$(nix hash file --type sha256 --base16 ./fetchurl.sh)

storePath=$(nix --store $other_store store add-file ./fetchurl.sh)

outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file:///no-such-dir/fetchurl.sh --argstr sha256 $hash --no-out-link --substituters $other_store)

# Test hashed mirrors with an SRI hash.
nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file:///no-such-dir/fetchurl.sh --argstr hash $(nix hash to-sri --type sha256 $hash) \
          --no-out-link --substituters $other_store

# Test unpacking a NAR.
rm -rf $TEST_ROOT/archive
mkdir -p $TEST_ROOT/archive
cp ./fetchurl.sh $TEST_ROOT/archive
chmod +x $TEST_ROOT/archive/fetchurl.sh
ln -s foo $TEST_ROOT/archive/symlink
nar=$TEST_ROOT/archive.nar
nix-store --dump $TEST_ROOT/archive > $nar

hash=$(nix-hash --flat --type sha256 $nar)

outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file://$nar --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

echo $outPath | grep -q 'xyzzy'

test -x $outPath/fetchurl.sh
test -L $outPath/symlink

nix-store --delete $outPath

# Test unpacking a compressed NAR.
narxz=$TEST_ROOT/archive.nar.xz
rm -f $narxz
xz --keep $nar
outPath=$(nix-build -vvvvv --expr 'import <nix/fetchurl.nix>' --argstr url file://$narxz --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

test -x $outPath/fetchurl.sh
test -L $outPath/symlink
