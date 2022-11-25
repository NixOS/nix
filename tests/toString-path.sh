source common.sh

mkdir -p $TEST_ROOT/foo
echo bla > $TEST_ROOT/foo/bar

[[ $(nix eval --raw --impure --expr "builtins.readFile (builtins.toString (builtins.fetchTree { type = \"path\"; path = \"$TEST_ROOT/foo\"; } + \"bar\"))") = bla ]]

[[ $(nix eval --json --impure --expr "builtins.readDir (builtins.toString (builtins.fetchTree { type = \"path\"; path = \"$TEST_ROOT/foo\"; }))") = '{"bar":"regular"}' ]]
