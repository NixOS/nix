source common.sh

clearStore

# Test nix-shell -A
export IMPURE_VAR=foo
export NIX_BUILD_SHELL=$SHELL
output=$(nix-shell --pure shell.nix -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"')

[ "$output" = " - foo - bar" ]

# Test nix-shell -p
output=$(NIX_PATH=nixpkgs=shell.nix nix-shell --pure -p foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = "foo bar" ]

# Test nix-shell shebang mode
sed -e "s|@ENV_PROG@|$(type -p env)|" shell.shebang.sh > $TEST_ROOT/shell.shebang.sh
chmod a+rx $TEST_ROOT/shell.shebang.sh

output=$($TEST_ROOT/shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]
