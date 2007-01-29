source common.sh

rm -rf $TEST_ROOT/filterin
mkdir $TEST_ROOT/filterin
mkdir $TEST_ROOT/filterin/foo
touch $TEST_ROOT/filterin/foo/bar
touch $TEST_ROOT/filterin/xyzzy
touch $TEST_ROOT/filterin/b
touch $TEST_ROOT/filterin/bak
touch $TEST_ROOT/filterin/bla.c.bak
ln -s xyzzy $TEST_ROOT/filterin/link

$NIX_BIN_DIR/nix-build ./filter-source.nix -o $TEST_ROOT/filterout

set -x
test ! -e $TEST_ROOT/filterout/foo/bar
test -e $TEST_ROOT/filterout/xyzzy
test -e $TEST_ROOT/filterout/bak
test ! -e $TEST_ROOT/filterout/bla.c.bak
test ! -L $TEST_ROOT/filterout/link
