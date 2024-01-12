source common.sh

clearStore

nix-instantiate --restrict-eval --eval -E '1 + 2'
(! nix-instantiate --eval --restrict-eval ./restricted.nix)
(! nix-instantiate --eval --restrict-eval <(echo '1 + 2'))
nix-instantiate --restrict-eval ./simple.nix -I src=.
nix-instantiate --restrict-eval ./simple.nix -I src1=simple.nix -I src2=config.nix -I src3=./simple.builder.sh

(! nix-instantiate --restrict-eval --eval -E 'builtins.readFile ./simple.nix')
nix-instantiate --restrict-eval --eval -E 'builtins.readFile ./simple.nix' -I src=../..

(! nix-instantiate --restrict-eval --eval -E 'builtins.readDir ../../src/nix-channel')
nix-instantiate --restrict-eval --eval -E 'builtins.readDir ../../src/nix-channel' -I src=../../src

expectStderr 1 nix-instantiate --restrict-eval --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in builtins.readFile <foo/simple.nix>' | grepQuiet "forbidden in restricted mode"
nix-instantiate --restrict-eval --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in builtins.readFile <foo/simple.nix>' -I src=.

p=$(nix eval --raw --expr "builtins.fetchurl file://$(pwd)/restricted.sh" --impure --restrict-eval --allowed-uris "file://$(pwd)")
cmp $p restricted.sh

(! nix eval --raw --expr "builtins.fetchurl file://$(pwd)/restricted.sh" --impure --restrict-eval)

(! nix eval --raw --expr "builtins.fetchurl file://$(pwd)/restricted.sh" --impure --restrict-eval --allowed-uris "file://$(pwd)/restricted.sh/")

nix eval --raw --expr "builtins.fetchurl file://$(pwd)/restricted.sh" --impure --restrict-eval --allowed-uris "file://$(pwd)/restricted.sh"

(! nix eval --raw --expr "builtins.fetchurl https://github.com/NixOS/patchelf/archive/master.tar.gz" --impure --restrict-eval)
(! nix eval --raw --expr "builtins.fetchTarball https://github.com/NixOS/patchelf/archive/master.tar.gz" --impure --restrict-eval)
(! nix eval --raw --expr "fetchGit git://github.com/NixOS/patchelf.git" --impure --restrict-eval)

ln -sfn $(pwd)/restricted.nix $TEST_ROOT/restricted.nix
[[ $(nix-instantiate --eval $TEST_ROOT/restricted.nix) == 3 ]]
(! nix-instantiate --eval --restrict-eval $TEST_ROOT/restricted.nix)
(! nix-instantiate --eval --restrict-eval $TEST_ROOT/restricted.nix -I $TEST_ROOT)
(! nix-instantiate --eval --restrict-eval $TEST_ROOT/restricted.nix -I .)
nix-instantiate --eval --restrict-eval $TEST_ROOT/restricted.nix -I $TEST_ROOT -I .

[[ $(nix eval --raw --impure --restrict-eval -I . --expr 'builtins.readFile "${import ./simple.nix}/hello"') == 'Hello World!' ]]

# Check that we can't follow a symlink outside of the allowed paths.
mkdir -p $TEST_ROOT/tunnel.d $TEST_ROOT/foo2
ln -sfn .. $TEST_ROOT/tunnel.d/tunnel
echo foo > $TEST_ROOT/bar

expectStderr 1 nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readFile <foo/tunnel/bar>" -I $TEST_ROOT/tunnel.d | grepQuiet "forbidden in restricted mode"

expectStderr 1 nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readDir <foo/tunnel/foo2>" -I $TEST_ROOT/tunnel.d | grepQuiet "forbidden in restricted mode"

# Reading the parents of allowed paths should show only the ancestors of the allowed paths.
[[ $(nix-instantiate --restrict-eval --eval -E "let __nixPath = [ { prefix = \"foo\"; path = $TEST_ROOT/tunnel.d; } ]; in builtins.readDir <foo/tunnel>" -I $TEST_ROOT/tunnel.d) == '{ "tunnel.d" = "directory"; }' ]]

# Check whether we can leak symlink information through directory traversal.
traverseDir="$(pwd)/restricted-traverse-me"
ln -sfn "$(pwd)/restricted-secret" "$(pwd)/restricted-innocent"
mkdir -p "$traverseDir"
goUp="..$(echo "$traverseDir" | sed -e 's,[^/]\+,..,g')"
output="$(nix eval --raw --restrict-eval -I "$traverseDir" \
    --expr "builtins.readFile \"$traverseDir/$goUp$(pwd)/restricted-innocent\"" \
    2>&1 || :)"
echo "$output" | grep "is forbidden"
echo "$output" | grepInverse -F restricted-secret

expectStderr 1 nix-instantiate --restrict-eval true ./dependencies.nix | grepQuiet "forbidden in restricted mode"
