source common.sh

clearStore

rm -rf $TEST_HOME

tarroot=$TEST_ROOT/tarball
rm -rf $tarroot
mkdir -p $tarroot
cp dependencies.nix $tarroot/default.nix
cp config.nix dependencies.builder*.sh $tarroot/

tarball=$TEST_ROOT/tarball.tar.xz
(cd $TEST_ROOT && tar c tarball) | xz > $tarball

nix-env -f file://$tarball -qa --out-path | grep -q dependencies

nix-build -o $TEST_ROOT/result file://$tarball

nix-build -o $TEST_ROOT/result '<foo>' -I foo=file://$tarball

nix-build -o $TEST_ROOT/result -E "import (fetchTarball file://$tarball)"

nix-instantiate --eval -E '1 + 2' -I fnord=file://no-such-tarball.tar.xz
nix-instantiate --eval -E 'with <fnord/xyzzy>; 1 + 2' -I fnord=file://no-such-tarball.tar.xz
(! nix-instantiate --eval -E '<fnord/xyzzy> 1' -I fnord=file://no-such-tarball.tar.xz)

nix-instantiate --eval -E '<fnord/config.nix>' -I fnord=file://no-such-tarball.tar.xz -I fnord=.
