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

checkFilter() {
    test ! -e $1/foo/bar
    test -e $1/xyzzy
    test -e $1/bak
    test ! -e $1/bla.c.bak
    test ! -L $1/link
}

nix-build ./filter-source.nix -o $TEST_ROOT/filterout1
checkFilter $TEST_ROOT/filterout1

nix-build ./path.nix -o $TEST_ROOT/filterout2
checkFilter $TEST_ROOT/filterout2
