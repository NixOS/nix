source common.sh

clearStore

# Test fetching a flat file.
hash=$(nix-hash --flat --type sha256 ./fetchurl.sh)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr sha256 $hash --no-out-link --hashed-mirrors '')

cmp $outPath fetchurl.sh

# Now using a base-64 hash.
clearStore

hash=$(nix hash-file --type sha512 --base64 ./fetchurl.sh)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr sha512 $hash --no-out-link --hashed-mirrors '')

cmp $outPath fetchurl.sh

# Now using an SRI hash.
clearStore

hash=$(nix hash-file ./fetchurl.sh)

[[ $hash =~ ^sha256- ]]

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$(pwd)/fetchurl.sh --argstr hash $hash --no-out-link --hashed-mirrors '')

cmp $outPath fetchurl.sh

# Test the hashed mirror feature.
clearStore

hash=$(nix hash-file --type sha512 --base64 ./fetchurl.sh)
hash32=$(nix hash-file --type sha512 --base16 ./fetchurl.sh)

mirror=$TMPDIR/hashed-mirror
rm -rf $mirror
mkdir -p $mirror/sha512
ln -s $(pwd)/fetchurl.sh $mirror/sha512/$hash32

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file:///no-such-dir/fetchurl.sh --argstr sha512 $hash --no-out-link --hashed-mirrors "file://$mirror")

# Test hashed mirrors with an SRI hash.
nix-build '<nix/fetchurl.nix>' --argstr url file:///no-such-dir/fetchurl.sh --argstr hash $(nix to-sri --type sha512 $hash) \
          --argstr name bla --no-out-link --hashed-mirrors "file://$mirror"

# Test unpacking a NAR.
rm -rf $TEST_ROOT/archive
mkdir -p $TEST_ROOT/archive
cp ./fetchurl.sh $TEST_ROOT/archive
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
