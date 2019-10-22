source common.sh

clearStore

# Test nix-shell -A
export IMPURE_VAR=foo
export SELECTED_IMPURE_VAR=baz
export NIX_BUILD_SHELL=$SHELL
output=$(nix-shell --pure shell.nix -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"')

[ "$output" = " - foo - bar" ]

# Test --keep
output=$(nix-shell --pure --keep SELECTED_IMPURE_VAR shell.nix -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $SELECTED_IMPURE_VAR"')

[ "$output" = " - foo - bar - baz" ]

# Test nix-shell on a .drv
[[ $(nix-shell --pure $(nix-instantiate shell.nix -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

[[ $(nix-shell --pure $(nix-instantiate shell.nix -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# Test nix-shell on a .drv symlink

# Legacy: absolute path and .drv extension required
nix-instantiate shell.nix -A shellDrv --indirect --add-root $TEST_ROOT/shell.drv
[[ $(nix-shell --pure $TEST_ROOT/shell.drv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# New behaviour: just needs to resolve to a derivation in the store
nix-instantiate shell.nix -A shellDrv --indirect --add-root $TEST_ROOT/shell
[[ $(nix-shell --pure $TEST_ROOT/shell --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# Test nix-shell -p
output=$(NIX_PATH=nixpkgs=shell.nix nix-shell --pure -p foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = "foo bar" ]

# Test nix-shell shebang mode
sed -e "s|@ENV_PROG@|$(type -p env)|" shell.shebang.sh > $TEST_ROOT/shell.shebang.sh
chmod a+rx $TEST_ROOT/shell.shebang.sh

output=$($TEST_ROOT/shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]

# Test nix-shell shebang mode for ruby
# This uses a fake interpreter that returns the arguments passed
# This, in turn, verifies the `rc` script is valid and the `load()` script (given using `-e`) is as expected.
sed -e "s|@SHELL_PROG@|$(type -p nix-shell)|" shell.shebang.rb > $TEST_ROOT/shell.shebang.rb
chmod a+rx $TEST_ROOT/shell.shebang.rb

output=$($TEST_ROOT/shell.shebang.rb abc ruby)
[ "$output" = '-e load("'"$TEST_ROOT"'/shell.shebang.rb") -- abc ruby' ]
