#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

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

# Test nix-shell shebang quoting
sed -e "s|@ENV_PROG@|$(type -P env)|" shell.shebang.nix > $TEST_ROOT/shell.shebang.nix
chmod a+rx $TEST_ROOT/shell.shebang.nix
$TEST_ROOT/shell.shebang.nix

mkdir $TEST_ROOT/lookup-test $TEST_ROOT/empty

cp $shellDotNix $TEST_ROOT/lookup-test/shell.nix
cp config.nix $TEST_ROOT/lookup-test/
echo 'abort "do not load default.nix!"' > $TEST_ROOT/lookup-test/default.nix

nix-shell $TEST_ROOT/lookup-test -A shellDrv --run 'echo "it works"' | grepQuiet "it works"
# https://github.com/NixOS/nix/issues/4529
nix-shell -I "testRoot=$TEST_ROOT" '<testRoot/lookup-test>' -A shellDrv --run 'echo "it works"' | grepQuiet "it works"

(
  cd $TEST_ROOT/empty;
  expectStderr 1 nix-shell | \
    grepQuiet "error.*no argument specified and no .*shell\.nix.* or .*default\.nix.* file found in the working directory"
)

expectStderr 1 nix-shell -I "testRoot=$TEST_ROOT" '<testRoot/empty>' |
  grepQuiet "error.*neither .*shell\.nix.* nor .*default\.nix.* found in .*/empty"

cat >$TEST_ROOT/lookup-test/shebangscript <<"EOF"
#!/usr/bin/env nix-shell
#!nix-shell -A shellDrv -i bash
[[ $VAR_FROM_NIX == bar ]]
echo "script works"
EOF
chmod +x $TEST_ROOT/lookup-test/shebangscript

$TEST_ROOT/lookup-test/shebangscript | grepQuiet "script works"

# https://github.com/NixOS/nix/issues/5431
mkdir $TEST_ROOT/marco{,/polo}
echo 'abort "marco/shell.nix must not be used, but its mere existence used to cause #5431"' > $TEST_ROOT/marco/shell.nix
cat >$TEST_ROOT/marco/polo/default.nix <<EOF
#!/usr/bin/env nix-shell
(import $TEST_ROOT/lookup-test/shell.nix {}).polo
EOF
chmod a+x $TEST_ROOT/marco/polo/default.nix
(cd $TEST_ROOT/marco && ./polo/default.nix | grepQuiet "Polo")


#####################
# Flake equivalents #
#####################

# Test 'nix develop'.
nix develop -f "$shellDotNix" shellDrv -c bash -c '[[ -n $stdenv ]]'

# Ensure `nix develop -c` preserves stdin
echo foo | nix develop -f "$shellDotNix" shellDrv -c cat | grepQuiet foo

# Ensure `nix develop -c` actually executes the command if stdout isn't a terminal
nix develop -f "$shellDotNix" shellDrv -c echo foo |& grepQuiet foo

# Test 'nix print-dev-env'.

nix print-dev-env -f "$shellDotNix" shellDrv > $TEST_ROOT/dev-env.sh
nix print-dev-env -f "$shellDotNix" shellDrv --json > $TEST_ROOT/dev-env.json

# Test with raw drv

shellDrv=$(nix-instantiate "$shellDotNix" -A shellDrv.out)

nix develop $shellDrv -c bash -c '[[ -n $stdenv ]]'

nix print-dev-env $shellDrv > $TEST_ROOT/dev-env2.sh
nix print-dev-env $shellDrv --json > $TEST_ROOT/dev-env2.json

diff $TEST_ROOT/dev-env{,2}.sh
diff $TEST_ROOT/dev-env{,2}.json

# Ensure `nix print-dev-env --json` contains variable assignments.
[[ $(jq -r .variables.arr1.value[2] $TEST_ROOT/dev-env.json) = '3 4' ]]

# Run tests involving `source <(nix print-dev-env)` in subshells to avoid modifying the current
# environment.

set -u

# Ensure `source <(nix print-dev-env)` modifies the environment.
(
    path=$PATH
    source $TEST_ROOT/dev-env.sh
    [[ -n $stdenv ]]
    [[ ${arr1[2]} = "3 4" ]]
    [[ ${arr2[1]} = $'\n' ]]
    [[ ${arr2[2]} = $'x\ny' ]]
    [[ $(fun) = blabla ]]
    [[ $PATH = $(jq -r .variables.PATH.value $TEST_ROOT/dev-env.json):$path ]]
)

# Ensure `source <(nix print-dev-env)` handles the case when PATH is empty.
(
    path=$PATH
    PATH=
    source $TEST_ROOT/dev-env.sh
    [[ $PATH = $(PATH=$path jq -r .variables.PATH.value $TEST_ROOT/dev-env.json) ]]
)

# Test nix-shell with ellipsis and no `inNixShell` argument (for backwards compat with old nixpkgs)
cat >$TEST_ROOT/shell-ellipsis.nix <<EOF
{ system ? "x86_64-linux", ... }@args:
assert (!(args ? inNixShell));
(import $shellDotNix { }).shellDrv
EOF
nix-shell $TEST_ROOT/shell-ellipsis.nix --run "true"
