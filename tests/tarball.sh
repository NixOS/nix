source common.sh

clearStore

export HOME=$TEST_ROOT/home
rm -rf $TEST_ROOT/home

tarroot=$TEST_ROOT/tarball
rm -rf $tarroot
mkdir -p $tarroot
cp dependencies.nix $tarroot/default.nix
cp config.nix dependencies.builder*.sh $tarroot/

tarball=$TEST_ROOT/tarball.tar.xz
(cd $TEST_ROOT && tar c tarball) | xz > $tarball

nix-env -f file://$tarball -qa --out-path | grep -q dependencies

nix-build -o $TMPDIR/result file://$tarball

nix-build -o $TMPDIR/result '<foo>' -I foo=file://$tarball

nix-build -o $TMPDIR/result -E "import (fetchTarball file://$tarball)"
