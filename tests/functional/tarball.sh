#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

rm -rf "$TEST_HOME"

tarroot=$TEST_ROOT/tarball
rm -rf "$tarroot"
mkdir -p "$tarroot"
cp dependencies.nix "$tarroot/default.nix"
cp "${config_nix}" dependencies.builder*.sh "$tarroot/"
touch -d '@1000000000' "$tarroot" "$tarroot"/*

hash=$(nix hash path "$tarroot")

test_tarball() {
    local ext="$1"
    local compressor="$2"

    tarball=$TEST_ROOT/tarball.tar$ext
    (cd "$TEST_ROOT" && GNUTAR_REPRODUCIBLE=1 tar --mtime="$tarroot"/default.nix --owner=0 --group=0 --numeric-owner --sort=name -c -f - tarball) | $compressor > "$tarball"

    nix-env -f file://"$tarball" -qa --out-path | grepQuiet dependencies

    nix-build -o "$TEST_ROOT"/result file://"$tarball"

    nix-build -o "$TEST_ROOT"/result '<foo>' -I foo=file://"$tarball"

    nix-build -o "$TEST_ROOT"/result -E "import (fetchTarball \"file://$tarball\")"
    # Do not re-fetch paths already present
    nix-build  -o "$TEST_ROOT"/result -E "import (fetchTarball { url = \"file:///does-not-exist/must-remain-unused/$tarball\"; sha256 = \"$hash\"; })"

    nix-build  -o "$TEST_ROOT"/result -E "import (fetchTree \"file://$tarball\")"
    nix-build  -o "$TEST_ROOT"/result -E "import (fetchTree { type = \"tarball\"; url = \"file://$tarball\"; })"
    nix-build  -o "$TEST_ROOT"/result -E "import (fetchTree { type = \"tarball\"; url = \"file://$tarball\"; narHash = \"$hash\"; })"

    [[ $(nix eval --impure --expr "(fetchTree \"file://$tarball\").lastModified") = 1000000000 ]]

    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = \"file://$tarball\"; narHash = \"$hash\"; })) ? submodules)" >&2
    nix-instantiate --strict --eval -E "!((import (fetchTree { type = \"tarball\"; url = \"file://$tarball\"; narHash = \"$hash\"; })) ? submodules)" 2>&1 | grep 'true'

    nix-instantiate --eval -E '1 + 2' -I fnord=file:///no-such-tarball.tar"$ext"
    nix-instantiate --eval -E 'with <fnord/xyzzy>; 1 + 2' -I fnord=file:///no-such-tarball"$ext"
    (! nix-instantiate --eval -E '<fnord/xyzzy> 1' -I fnord=file:///no-such-tarball"$ext")

    nix-instantiate --eval -E '<fnord/config.nix>' -I fnord=file:///no-such-tarball"$ext" -I fnord="${_NIX_TEST_BUILD_DIR}"

    # Ensure that the `name` attribute isn’t accepted as that would mess
    # with the content-addressing
    (! nix-instantiate --eval -E "fetchTree { type = \"tarball\"; url = \"file://$tarball\"; narHash = \"$hash\"; name = \"foo\"; }")

    store_path=$(nix store prefetch-file --json "file://$tarball" | jq -r .storePath)
    if ! cmp -s "$store_path" "$tarball"; then
        echo "prefetched tarball differs from original: $store_path vs $tarball" >&2
        exit 1
    fi
    store_path2=$(nix store prefetch-file --json --unpack "file://$tarball" | jq -r .storePath)
    diff_output=$(diff -r "$store_path2" "$tarroot")
    if [ -n "$diff_output" ]; then
        echo "prefetched tarball differs from original: $store_path2 vs $tarroot" >&2
        echo "$diff_output"
        exit 1
    fi
}

test_tarball '' cat
test_tarball .xz xz
test_tarball .gz gzip

# Test hard links.
# All entries in tree.tar.gz refer to the same file, and all have the same inode when unpacked by GNU tar.
# We don't preserve the hard links, because that's an optimization we think is not worth the complexity,
# so we only make sure that the contents are copied correctly.
json=$(nix flake prefetch --json "tarball+file://$(pwd)/tree.tar.gz" --out-link "$TEST_ROOT/result")
[[ $json =~ ^'{"hash":"sha256-'.*'","locked":{"lastModified":'.*',"narHash":"sha256-'.*'","type":"tarball","url":"file:///'.*'/tree.tar.gz"},"original":{"type":"tarball","url":"file:///'.*'/tree.tar.gz"},"storePath":"'.*'/store/'.*'-source"}'$ ]]
[[ $(cat "$TEST_ROOT/result/a/b/foo") = bar ]]
[[ $(cat "$TEST_ROOT/result/a/b/xyzzy") = bar ]]
[[ $(cat "$TEST_ROOT/result/a/yyy") = bar ]]
[[ $(cat "$TEST_ROOT/result/a/zzz") = bar ]]
[[ $(cat "$TEST_ROOT/result/c/aap") = bar ]]
[[ $(cat "$TEST_ROOT/result/fnord") = bar ]]

# Test a tarball that has multiple top-level directories.
rm -rf "$TEST_ROOT/tar_root"
mkdir -p "$TEST_ROOT/tar_root" "$TEST_ROOT/tar_root/foo" "$TEST_ROOT/tar_root/bar"
tar cvf "$TEST_ROOT/tar.tar" -C "$TEST_ROOT/tar_root" .
path="$(nix flake prefetch --json "tarball+file://$TEST_ROOT/tar.tar" | jq -r .storePath)"
[[ -d "$path/foo" ]]
[[ -d "$path/bar" ]]

# Test a tarball that has a single regular file.
rm -rf "$TEST_ROOT/tar_root"
mkdir -p "$TEST_ROOT/tar_root"
echo bar > "$TEST_ROOT/tar_root/foo"
chmod +x "$TEST_ROOT/tar_root/foo"
tar cvf "$TEST_ROOT/tar.tar" -C "$TEST_ROOT/tar_root" .
path="$(nix flake prefetch --refresh --json "tarball+file://$TEST_ROOT/tar.tar" | jq -r .storePath)"
[[ $(cat "$path/foo") = bar ]]

# Test a tarball with non-contiguous directory entries.
rm -rf "$TEST_ROOT/tar_root"
mkdir -p "$TEST_ROOT/tar_root/a/b"
echo foo > "$TEST_ROOT/tar_root/a/b/foo"
echo bla > "$TEST_ROOT/tar_root/bla"
tar cvf "$TEST_ROOT/tar.tar" -C "$TEST_ROOT/tar_root" .
echo abc > "$TEST_ROOT/tar_root/bla"
echo xyzzy > "$TEST_ROOT/tar_root/a/b/xyzzy"
tar rvf "$TEST_ROOT/tar.tar" -C "$TEST_ROOT/tar_root" ./a/b/xyzzy ./bla
path="$(nix flake prefetch --refresh --json "tarball+file://$TEST_ROOT/tar.tar" | jq -r .storePath)"
[[ $(cat "$path/a/b/xyzzy") = xyzzy ]]
[[ $(cat "$path/a/b/foo") = foo ]]
[[ $(cat "$path/bla") = abc ]]

# Test that unpacking an empty file does not segfault (see https://github.com/NixOS/nix/issues/15116).
touch "$TEST_ROOT/empty"
expectStderr 1 nix store prefetch-file --unpack "file://$TEST_ROOT/empty" | grepQuiet "archive.*is empty"
