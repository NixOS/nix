source common.sh

clearStore

# Test nix-shell -A
export IMPURE_VAR=foo
export SELECTED_IMPURE_VAR=baz
export NIX_BUILD_SHELL=$SHELL
output=$(nix-shell --pure shell.nix -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"')

[ "$output" = " - foo - bar - true" ]

# Test --keep
output=$(nix-shell --pure --keep SELECTED_IMPURE_VAR shell.nix -A shellDrv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $SELECTED_IMPURE_VAR"')

[ "$output" = " - foo - bar - baz" ]

# Test nix-shell on a .drv
[[ $(nix-shell --pure $(nix-instantiate shell.nix -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

[[ $(nix-shell --pure $(nix-instantiate shell.nix -A shellDrv) --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX - $TEST_inNixShell"') = " - foo - bar - false" ]]

# Test nix-shell on a .drv symlink

# Legacy: absolute path and .drv extension required
nix-instantiate shell.nix -A shellDrv --add-root $TEST_ROOT/shell.drv
[[ $(nix-shell --pure $TEST_ROOT/shell.drv --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# New behaviour: just needs to resolve to a derivation in the store
nix-instantiate shell.nix -A shellDrv --add-root $TEST_ROOT/shell
[[ $(nix-shell --pure $TEST_ROOT/shell --run \
    'echo "$IMPURE_VAR - $VAR_FROM_STDENV_SETUP - $VAR_FROM_NIX"') = " - foo - bar" ]]

# Test nix-shell --tools
output=$(NIX_PATH=nixpkgs=shell.nix nix-shell --pure --tools foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = "foo bar" ]

# Test nix-shell -p
output=$(NIX_PATH=nixpkgs=shell.nix nix-shell --pure -p flibble flobble --run 'for bi in $buildInputs; do cat $bi/data; done')
[ "$(echo $output)" = "flibble flobble" ]

# Test mix of nix-shell --tools, --inputs
# make sure "strictDeps" works
output=$(NIX_PATH=nixpkgs=shell.nix:strictDeps=shell.nix nix-shell --pure -p foo bar --run 'echo "$(foo) $(bar)"')
[ "$output" = " " ]
# leading arguments are inputs
output=$(NIX_PATH=nixpkgs=shell.nix:strictDeps=shell.nix nix-shell --pure flibble flobble --tools --run 'for bi in $buildInputs; do cat $bi/data; done')
[ "$(echo $output)" = "flibble flobble" ]
# swapping inputs swaps the output value
output=$(NIX_PATH=nixpkgs=shell.nix:strictDeps=shell.nix nix-shell --pure flobble flibble --tools --run 'for bi in $buildInputs; do cat $bi/data; done')
[ "$(echo $output)" = "flobble flibble" ]
# leading arguments are not tools (under strictDeps)
output=$(NIX_PATH=nixpkgs=shell.nix:strictDeps=shell.nix nix-shell --pure foo bar --tools --run 'echo "$(foo) $(bar)"')
[ "$output" = " " ]
# tools and inputs can be mixed
output=$(NIX_PATH=nixpkgs=shell.nix:strictDeps=shell.nix nix-shell --tools foo --inputs flibble --tools bar --inputs flobble --tools --inputs --run 'echo $(foo) $(bar); for bi in $buildInputs; do cat $bi/data; done')
[ "$(echo $output)" = "foo bar flibble flobble" ]


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

# Test 'nix develop'.
nix develop -f shell.nix shellDrv -c bash -c '[[ -n $stdenv ]]'

# Ensure `nix develop -c` preserves stdin
echo foo | nix develop -f shell.nix shellDrv -c cat | grep -q foo

# Ensure `nix develop -c` actually executes the command if stdout isn't a terminal
nix develop -f shell.nix shellDrv -c echo foo |& grep -q foo

# Test 'nix print-dev-env'.
source <(nix print-dev-env -f shell.nix shellDrv)
[[ -n $stdenv ]]
