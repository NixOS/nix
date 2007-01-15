source common.sh

rm -rf $TEST_ROOT/filterin
mkdir $TEST_ROOT/filterin
mkdir $TEST_ROOT/filterin/foo
touch $TEST_ROOT/filterin/foo/bar
touch $TEST_ROOT/filterin/xyzzy
ln -s xyzzy $TEST_ROOT/filterin/link

$NIX_BIN_DIR/nix-build ./filter-source.nix -o $TEST_ROOT/filterout

set -x
test ! -e $TEST_ROOT/filterout/foo/bar
test -e $TEST_ROOT/filterout/xyzzy
test ! -L $TEST_ROOT/filterout/link
