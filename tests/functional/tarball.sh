source common.sh

clearStore

rm -rf $TEST_HOME

tarroot=$TEST_ROOT/tarball
rm -rf $tarroot
mkdir -p $tarroot
cp dependencies.nix $tarroot/default.nix
cp config.nix dependencies.builder*.sh $tarroot/
touch -d '@1000000000' $tarroot $tarroot/*

hash=$(nix hash path $tarroot)

test_tarball() {
    local ext="$1"
    local compressor="$2"

    tarball=$TEST_ROOT/tarball.tar$ext
    (cd $TEST_ROOT && GNUTAR_REPRODUCIBLE= tar --mtime=$tarroot/default.nix --owner=0 --group=0 --numeric-owner --sort=name -c -f - tarball) | $compressor > $tarball

    nix-env -f file://$tarball -qa --out-path | grepQuiet dependencies

    nix-build -o $TEST_ROOT/result file://$tarball

    nix-build -o $TEST_ROOT/result '<foo>' -I foo=file://$tarball

    nix-build -o $TEST_ROOT/result -E "import (fetchTarball file://$tarball)"
    # Do not re-fetch paths already present
    nix-build  -o $TEST_ROOT/result -E "import (fetchTarball { url = file:///does-not-exist/must-remain-unused/$tarball; sha256 = \"$hash\"; })"

    nix-build  -o $TEST_ROOT/result -E "import (fetchTree file://$tarball)"
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; })"
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })"
    # Do not re-fetch paths already present
    nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file:///does-not-exist/must-remain-unused/$tarball; narHash = \"$hash\"; })"
    expectStderr 102 nix-build  -o $TEST_ROOT/result -E "import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"sha256-xdKv2pq/IiwLSnBBJXW8hNowI4MrdZfW+SYqDQs7Tzc=\"; })" | grep 'NAR hash mismatch in input'

    [[ $(nix eval --impure --expr "(fetchTree file://$tarball).lastModified") = 1000000000 ]]

    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })) ? submodules)" >&2
    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; })) ? submodules)" 2>&1 | grep 'true'

    nix-instantiate --eval -E '1 + 2' -I fnord=file:///no-such-tarball.tar$ext
    nix-instantiate --eval -E 'with <fnord/xyzzy>; 1 + 2' -I fnord=file:///no-such-tarball$ext
    (! nix-instantiate --eval -E '<fnord/xyzzy> 1' -I fnord=file:///no-such-tarball$ext)

    nix-instantiate --eval -E '<fnord/config.nix>' -I fnord=file:///no-such-tarball$ext -I fnord=.

    # Ensure that the `name` attribute isn’t accepted as that would mess
    # with the content-addressing
    (! nix-instantiate --eval -E "fetchTree { type = \"tarball\"; url = file://$tarball; narHash = \"$hash\"; name = \"foo\"; }")

}

test_tarball '' cat
test_tarball .xz xz
test_tarball .gz gzip
