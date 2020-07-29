source common.sh

clearStore

rm -rf $TEST_HOME

tarroot=$TEST_ROOT/tarball
rm -rf $tarroot
mkdir -p $tarroot
cp dependencies.nix $tarroot/default.nix
cp config.nix dependencies.builder*.sh $tarroot/

hash=$(nix hash-path $tarroot)

test_tarball() {
    local ext="$1"
    local compressor="$2"

    tarball=$TEST_ROOT/tarball.tar$ext
    (cd $TEST_ROOT && tar c tarball) | $compressor > $tarball

    nix-env -f file://$tarball -qa --out-path | grep -q dependencies

    nix-build -o $TEST_ROOT/result file://$tarball

    nix-build -o $TEST_ROOT/result '<foo>' -I foo=file://$tarball

    nix-build -o $TEST_ROOT/result -E "import (fetchTarball file://$tarball)"

    nix-build  -o $TEST_ROOT/result -E "import (fetchTree file://$tarball)"
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; })"
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })"
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"sha256-xdKv2pq/IiwLSnBBJXW8hNowI4MrdZfW+SYqDQs7Tzc=\"; })" 2>&1 | grep 'NAR hash mismatch in input'

    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })) ? submodules)" >&2
    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })) ? submodules)" 2>&1 | grep 'true'

    nix-instantiate --eval -E '1 + 2' -I fnord=file://no-such-tarball.tar$ext
    nix-instantiate --eval -E 'with <fnord/xyzzy>; 1 + 2' -I fnord=file://no-such-tarball$ext
    (! nix-instantiate --eval -E '<fnord/xyzzy> 1' -I fnord=file://no-such-tarball$ext)

    nix-instantiate --eval -E '<fnord/config.nix>' -I fnord=file://no-such-tarball$ext -I fnord=.
}

test_tarball '' cat
test_tarball .xz xz
test_tarball .gz gzip

rm -rf $TEST_ROOT/tmp
mkdir -p $TEST_ROOT/tmp
(! TMPDIR=$TEST_ROOT/tmp XDG_RUNTIME_DIR=$TEST_ROOT/tmp nix-env -f file://$(pwd)/bad.tar.xz -qa --out-path)
(! [ -e $TEST_ROOT/tmp/bad ])
