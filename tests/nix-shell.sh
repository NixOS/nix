source common.sh

clearStore

if [[ -n ${CONTENT_ADDRESSED:-} ]]; then
    shellDotNix="$PWD/ca-shell.nix"
else
    shellDotNix="$PWD/shell.nix"
fi

export NIX_PATH=nixpkgs="$shellDotNix"

# Test nix-shell -A
export IMPURE_VAR=foo
export SELECTED_IMPURE_VAR=baz

output=$(nix-shell --pure "$shellDotNix" -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"')

[ "$output" = " - foo - bar - true" ]

# Test --keep
output=$(nix-shell --pure --keep SELECTED_IMPURE_VAR "$shellDotNix" -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $SELECTED_IMPURE_VAR"')

[ "$output" = " - foo - bar - baz" ]

# Test nix-shell on a .drv
[[ $(nix-shell --pure $(nix-instantiate "$shellDotNix" -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

[[ $(nix-shell --pure $(nix-instantiate "$shellDotNix" -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

# Test nix-shell on a .drv symlink

# Legacy: absolute path and .drv extension required
nix-instantiate "$shellDotNix" -A shellDrv --add-root $TEST_ROOT/shell.drv
[[ $(nix-shell --pure $TEST_ROOT/shell.drv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# New behaviour: just needs to resolve to a derivation in the store
nix-instantiate "$shellDotNix" -A shellDrv --add-root $TEST_ROOT/shell
[[ $(nix-shell --pure $TEST_ROOT/shell --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# Test nix-shell -p
output=$(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = "foo bar" ]

# Test nix-shell -p --arg x y
output=$(NIX_PATH=nixpkgs="$shellDotNix" nix-shell --pure -p foo --argstr fooContents baz --run 'echo "$(foo)"')
[ "$output" = "baz" ]

# Test nix-shell shebang mode
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.sh > $TEST_ROOT/shell.shebang.sh
chmod a+rx $TEST_ROOT/shell.shebang.sh

output=$($TEST_ROOT/shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]

# Test nix-shell shebang mode again with metacharacters in the filename.
# First word of filename is chosen to not match any file in the test root.
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.sh > $TEST_ROOT/spaced\ \\\'\"shell.shebang.sh
chmod a+rx $TEST_ROOT/spaced\ \\\'\"shell.shebang.sh

output=$($TEST_ROOT/spaced\ \\\'\"shell.shebang.sh abc def)
[ "$output" = "foo bar abc def" ]

# Test nix-shell shebang mode for ruby
# This uses a fake interpreter that returns the arguments passed
# This, in turn, verifies the `rc` script is valid and the `load()` script (given using `-e`) is as expected.
sed -e "s|@SHELL_PROG@|$(type -P nix-shell)|" shell.shebang.rb > $TEST_ROOT/shell.shebang.rb
chmod a+rx $TEST_ROOT/shell.shebang.rb

output=$($TEST_ROOT/shell.shebang.rb abc ruby)
[ "$output" = '-e load(ARGV.shift) -- '"$TEST_ROOT"'/shell.shebang.rb abc ruby' ]

# Test nix-shell shebang mode for ruby again with metacharacters in the filename.
# Note: fake interpreter only space-separates args without adding escapes to its output.
sed -e "s|@SHELL_PROG@|$(type -P nix-shell)|" shell.shebang.rb > $TEST_ROOT/spaced\ \\\'\"shell.shebang.rb
chmod a+rx $TEST_ROOT/spaced\ \\\'\"shell.shebang.rb

output=$($TEST_ROOT/spaced\ \\\'\"shell.shebang.rb abc ruby)
[ "$output" = '-e load(ARGV.shift) -- '"$TEST_ROOT"'/spaced \'\''"shell.shebang.rb abc ruby' ]

# Test 'nix develop'.
nix develop -f "$shellDotNix" shellDrv -c bash -c '[[ -n $stdenv ]]'

# Ensure `nix develop -c` preserves stdin
echo foo | nix develop -f "$shellDotNix" shellDrv -c cat | grep -q foo

# Ensure `nix develop -c` actually executes the command if stdout isn't a terminal
nix develop -f "$shellDotNix" shellDrv -c echo foo |& grep -q foo

# Test 'nix print-dev-env'.
[[ $(nix print-dev-env -f "$shellDotNix" shellDrv --json | jq -r .variables.arr1.value[2]) = '3 4' ]]

source <(nix print-dev-env -f "$shellDotNix" shellDrv)
[[ -n $stdenv ]]
[[ ${arr1[2]} = "3 4" ]]
[[ ${arr2[1]} = $'\n' ]]
[[ ${arr2[2]} = $'x\ny' ]]
[[ $(fun) = blabla ]]
