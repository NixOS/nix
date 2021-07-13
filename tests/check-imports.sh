source common.sh

rm -rf $TEST_ROOT/example
mkdir -p $TEST_ROOT/example/{,alt-}dir
echo '"good"' > $TEST_ROOT/example/good.nix
echo '"good"' > $TEST_ROOT/example/dir/alt-good.nix
echo 'import ../good.nix' > $TEST_ROOT/example/dir/import-good.nix
echo 'import ./alt-good.nix' > $TEST_ROOT/example/dir/default.nix
ln -s dir/import-good.nix $TEST_ROOT/example/
ln -s . $TEST_ROOT/example/dir/subdir
ln -s good.nix $TEST_ROOT/example/good-link.nix
ln -s good-link.nix $TEST_ROOT/example/good-link-link.nix
ln -s ../good.nix $TEST_ROOT/example/dir/good-link.nix
ln -s ../dir/default.nix $TEST_ROOT/example/alt-dir/

testGoodEval() {
    test "`nix eval --impure --raw "$@"`" = "good"
}

testImportFile() {
    testGoodEval -f "$1"
    testGoodEval --expr "(import $1)"
}

# direct import
testImportFile $TEST_ROOT/example/good.nix

# dir import
testImportFile $TEST_ROOT/example/dir

# two-level relative import
testImportFile $TEST_ROOT/example/dir/import-good.nix

# symlink
testImportFile $TEST_ROOT/example/good-link.nix

# relative symlink
testImportFile $TEST_ROOT/example/dir/good-link.nix

# two-level symlink
testImportFile $TEST_ROOT/example/good-link-link.nix

# symlink dir
testImportFile $TEST_ROOT/example/dir/subdir/alt-good.nix

# symlink dir import
testImportFile $TEST_ROOT/example/dir/subdir

# two-level relative import from path with symlink dir
testImportFile $TEST_ROOT/example/dir/subdir/import-good.nix

# relative traverse of symlink dir
# FIXME: testImportFile $TEST_ROOT/example/dir/subdir/../good.nix

# dir import with symlinked default.nix
testImportFile $TEST_ROOT/example/alt-dir
