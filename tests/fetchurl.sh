export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

# Test fetching a flat file.
hash=$(nix-hash --flat --type sha256 $NIX_TEST_ROOT/fetchurl.sh)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$NIX_TEST_ROOT/fetchurl.sh --argstr sha256 $hash --no-out-link)

cmp $outPath $NIX_TEST_ROOT/fetchurl.sh

# Now using a base-64 hash.
clearStore

hash=$(nix hash-file --type sha512 --base64 ./fetchurl.sh)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr sha512 $hash --no-out-link)

cmp $outPath fetchurl.sh

# Test unpacking a NAR.
rm -rf $TEST_ROOT/archive
mkdir -p $TEST_ROOT/archive
cp $NIX_TEST_ROOT/fetchurl.sh $TEST_ROOT/archive
chmod +x $TEST_ROOT/archive/fetchurl.sh
ln -s foo $TEST_ROOT/archive/symlink
nar=$TEST_ROOT/archive.nar
nix-store --dump $TEST_ROOT/archive > $nar

hash=$(nix-hash --flat --type sha256 $nar)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$nar --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

echo $outPath | grep -q 'xyzzy'

test -x $outPath/fetchurl.sh
test -L $outPath/symlink

nix-store --delete $outPath

# Test unpacking a compressed NAR.
narxz=$TEST_ROOT/archive.nar.xz
rm -f $narxz
xz --keep $nar
outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$narxz --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

test -x $outPath/fetchurl.sh
test -L $outPath/symlink
