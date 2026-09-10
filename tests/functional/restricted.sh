#!/usr/bin/env bash

source common.sh

nix-instantiate --restrict-eval --eval -E '1 + 2'
(! nix-instantiate --eval --restrict-eval ./restricted.nix)
TMPFILE=$(mktemp -p "$TEST_ROOT"); echo '1 + 2' >"$TMPFILE"; (! nix-instantiate --eval --restrict-eval "$TMPFILE");

mkdir -p "$TEST_ROOT/nix"
cp ./simple.nix "$TEST_ROOT/nix"
cp ./simple.builder.sh "$TEST_ROOT/nix"
cp "${config_nix}" "$TEST_ROOT/nix"
cd "$TEST_ROOT/nix"

nix-instantiate --restrict-eval ./simple.nix -I src=.
nix-instantiate --restrict-eval ./simple.nix -I src1=./simple.nix -I src2=./config.nix -I src3=./simple.builder.sh

# no default NIX_PATH
(unset NIX_PATH; ! nix-instantiate --restrict-eval --find-file .)

(! nix-instantiate --restrict-eval --eval -E 'builtins.readFile ./simple.nix')
nix-instantiate --restrict-eval --eval -E 'builtins.readFile ./simple.nix' -I src=../..

expectStderr 1 nix-instantiate --restrict-eval --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in builtins.readFile <foo/simple.nix>' | grepQuiet "forbidden in restricted mode"
nix-instantiate --restrict-eval --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in builtins.readFile <foo/simple.nix>' -I src=.

p=$(nix eval --raw --expr "builtins.fetchurl \"file://${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval --allowed-uris "file://${_NIX_TEST_SOURCE_DIR}")
cmp "$p" "${_NIX_TEST_SOURCE_DIR}/restricted.sh"

(! nix eval --raw --expr "builtins.fetchurl \"file://${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval)

(! nix eval --raw --expr "builtins.fetchurl \"file://${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval --allowed-uris "file://${_NIX_TEST_SOURCE_DIR}/restricted.sh/")

nix eval --raw --expr "builtins.fetchurl \"file://${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval --allowed-uris "file://${_NIX_TEST_SOURCE_DIR}/restricted.sh"

# `allowed-uris` should also grant `readFile`/`readDir` access, not just fetchers (#2596).
(! nix eval --raw --expr "builtins.readFile \"${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval)
[[ $(nix eval --raw --expr "builtins.readFile \"${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval --allowed-uris "${_NIX_TEST_SOURCE_DIR}") == "$(cat "${_NIX_TEST_SOURCE_DIR}/restricted.sh")" ]]
[[ $(nix eval --expr "builtins.readDir \"${_NIX_TEST_SOURCE_DIR}\"" --impure --restrict-eval --allowed-uris "${_NIX_TEST_SOURCE_DIR}" --json | jq -r 'keys | length') -gt 0 ]]
(! nix eval --raw --expr "builtins.readFile \"${_NIX_TEST_SOURCE_DIR}/restricted.sh\"" --impure --restrict-eval --allowed-uris "${_NIX_TEST_SOURCE_DIR}/other-dir")

# A symlink under an `allowed-uris`-granted directory that points elsewhere in
# that same directory should still resolve, not spuriously fail as unresolvable.
mkdir -p "$TEST_ROOT/allowed-uris-dir"
echo -n "hello" > "$TEST_ROOT/allowed-uris-dir/target"
ln -sfn "$TEST_ROOT/allowed-uris-dir/target" "$TEST_ROOT/allowed-uris-dir/link"
[[ $(nix eval --raw --expr "builtins.readFile \"$TEST_ROOT/allowed-uris-dir/link\"" --impure --restrict-eval --allowed-uris "$TEST_ROOT/allowed-uris-dir") == "hello" ]]

# `allowed-uris` in `file://` form should also grant `readFile` etc., not just
# fetchers, matching the bare-path form.
[[ $(nix eval --raw --expr "builtins.readFile \"$TEST_ROOT/allowed-uris-dir/target\"" --impure --restrict-eval --allowed-uris "file://$TEST_ROOT/allowed-uris-dir") == "hello" ]]

# `import` of a file reached through a symlink should also be granted by
# `allowed-uris`, not just a direct, non-symlink path.
echo '"imported-ok"' > "$TEST_ROOT/allowed-uris-dir/target.nix"
ln -sfn "$TEST_ROOT/allowed-uris-dir/target.nix" "$TEST_ROOT/allowed-uris-dir/link.nix"
[[ $(nix eval --raw --expr "import \"$TEST_ROOT/allowed-uris-dir/link.nix\"" --impure --restrict-eval --allowed-uris "$TEST_ROOT/allowed-uris-dir") == "imported-ok" ]]

# A relative import inside a file reached through a symlinked directory
# should resolve against the symlink's logical location, not the resolved
# target's physical location, even when `allowed-uris` (not `-I`) is what
# grants access.
mkdir -p "$TEST_ROOT/allowed-uris-symlink/foo/lib" "$TEST_ROOT/allowed-uris-symlink/overlays"
echo '"sibling-ok"' > "$TEST_ROOT/allowed-uris-symlink/foo/lib/default.nix"
echo 'import ../lib' > "$TEST_ROOT/allowed-uris-symlink/overlays/overlay.nix"
ln -sfn "../overlays" "$TEST_ROOT/allowed-uris-symlink/foo/overlays"
[[ $(nix eval --raw --expr "import \"$TEST_ROOT/allowed-uris-symlink/foo/overlays/overlay.nix\"" --impure --restrict-eval --allowed-uris "$TEST_ROOT/allowed-uris-symlink") == "sibling-ok" ]]

(! nix eval --raw --expr "builtins.fetchurl \"https://github.com/NixOS/patchelf/archive/master.tar.gz\"" --impure --restrict-eval)
(! nix eval --raw --expr "builtins.fetchTarball \"https://github.com/NixOS/patchelf/archive/master.tar.gz\"" --impure --restrict-eval)
(! nix eval --raw --expr "fetchGit \"git://github.com/NixOS/patchelf.git\"" --impure --restrict-eval)

ln -sfn "${_NIX_TEST_SOURCE_DIR}/restricted.nix" "$TEST_ROOT/restricted.nix"
[[ $(nix-instantiate --eval "$TEST_ROOT"/restricted.nix) == 3 ]]
(! nix-instantiate --eval --restrict-eval "$TEST_ROOT"/restricted.nix)
(! nix-instantiate --eval --restrict-eval "$TEST_ROOT"/restricted.nix -I "$TEST_ROOT")
(! nix-instantiate --eval --restrict-eval "$TEST_ROOT"/restricted.nix -I .)
nix-instantiate --eval --restrict-eval "$TEST_ROOT/restricted.nix" -I "$TEST_ROOT" -I "${_NIX_TEST_SOURCE_DIR}"

# shellcheck disable=SC2016
[[ $(nix eval --raw --impure --restrict-eval -I . --expr 'builtins.readFile "${import ./simple.nix}/hello"') == 'Hello World!' ]]

# Check that we can't follow a symlink outside of the allowed paths.
mkdir -p "$TEST_ROOT"/tunnel.d "$TEST_ROOT"/foo2
ln -sfn .. "$TEST_ROOT"/tunnel.d/tunnel
echo foo > "$TEST_ROOT"/bar

expectStderr 1 nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readFile <foo/tunnel/bar>" -I "$TEST_ROOT"/tunnel.d | grepQuiet "forbidden in restricted mode"

expectStderr 1 nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readDir <foo/tunnel/foo2>" -I "$TEST_ROOT"/tunnel.d | grepQuiet "forbidden in restricted mode"

# Reading the parents of allowed paths should show only the ancestors of the allowed paths.
[[ $(nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readDir <foo/tunnel>" -I "$TEST_ROOT"/tunnel.d) == '{ "tunnel.d" = "directory"; }' ]]

# Check whether we can leak symlink information through directory traversal.
traverseDir="$TEST_ROOT/restricted-traverse-me"
ln -sfn "$TEST_ROOT/restricted-secret" "$TEST_ROOT/restricted-innocent"
mkdir -p "$traverseDir"
# shellcheck disable=SC2001
goUp="..$(echo "$traverseDir" | sed -e 's,[^/]\+,..,g')"
output="$(nix eval --raw --restrict-eval -I "$traverseDir" \
    --expr "builtins.readFile \"$traverseDir/$goUp${_NIX_TEST_SOURCE_DIR}/restricted-innocent\"" \
    2>&1 || :)"
echo "$output" | grep "is forbidden"
echo "$output" | grepInverse -F restricted-secret

expectStderr 1 nix-instantiate --restrict-eval true ./dependencies.nix | grepQuiet "forbidden in restricted mode"
