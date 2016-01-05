source common.sh

clearStore

# Test fetching a flat file.
TEST_FILE=test_fetchurl.sh
hash=$(nix-hash --flat --type sha256 "./${TEST_FILE}")

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$(pwd)/${TEST_FILE} --argstr sha256 $hash --no-out-link)

cmp $outPath ${TEST_FILE}

# Test unpacking a NAR.
rm -rf $TEST_ROOT/archive
mkdir -p $TEST_ROOT/archive
cp "./${TEST_FILE}" $TEST_ROOT/archive
chmod +x $TEST_ROOT/archive/${TEST_FILE}
ln -s foo $TEST_ROOT/archive/symlink
nar=$TEST_ROOT/archive.nar
nix-store --dump $TEST_ROOT/archive > $nar

hash=$(nix-hash --flat --type sha256 $nar)

outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$nar --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

echo $outPath | grep -q 'xyzzy'

ls $outPath
test -x $outPath/${TEST_FILE}
test -L $outPath/symlink

nix-store --delete $outPath

# Test unpacking a compressed NAR.
narxz=$TEST_ROOT/archive.nar.xz
rm -f $narxz
xz --keep $nar
outPath=$(nix-build '<nix/fetchurl.nix>' --argstr url file://$narxz --argstr sha256 $hash \
          --arg unpack true --argstr name xyzzy --no-out-link)

test -x $outPath/${TEST_FILE}
test -L $outPath/symlink
