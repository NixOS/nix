#!/usr/bin/env bash

source ../common.sh

TODO_NixOS

clearStore
rm -rf "$TEST_HOME/.cache" "$TEST_HOME/.config" "$TEST_HOME/.local"

# Create flake under test.
cp ../shell-hello.nix ../config.nix "$TEST_HOME/"
cat <<EOF >"$TEST_HOME/flake.nix"
{
    inputs.nixpkgs.url = "$TEST_HOME/nixpkgs";
    outputs = {self, nixpkgs}: {
      packages.$system.hello = (import ./config.nix).mkDerivation {
        name = "hello";
        outputs = [ "out" "dev" ];
        meta.outputsToInstall = [ "out" ];
        buildCommand = "";
      };
    };
}
EOF

# Create fake nixpkgs flake.
mkdir -p "$TEST_HOME/nixpkgs"
cp ../config.nix ../shell.nix "$TEST_HOME/nixpkgs"
cat <<EOF >"$TEST_HOME/nixpkgs/flake.nix"
{
    outputs = {self}: {
      legacyPackages.$system.bashInteractive = (import ./shell.nix {}).bashInteractive;
    };
}
EOF

cd "$TEST_HOME"

# Test whether `nix develop` passes through environment variables.
[[ "$(
    ENVVAR=a nix develop --no-write-lock-file .#hello <<EOF
echo "\$ENVVAR"
EOF
)" = "a" ]]

# Test whether `nix develop --ignore-env` does _not_ pass through environment variables.
[[ -z "$(
    ENVVAR=a nix develop --ignore-env --no-write-lock-file .#hello <<EOF
echo "\$ENVVAR"
EOF
)" ]]

# Test wether `--keep-env-var` keeps the environment variable.
(
  expect='BAR'
  got="$(FOO='BAR' nix develop --ignore-env --keep-env-var FOO --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test wether `--set-env-var` sets the environment variable.
(
  expect='BAR'
  got="$(nix develop --ignore-env --set-env-var FOO 'BAR' --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test wether multiple `--unset-env-var` and `--set-env-var` cancle out
(
  expect=''
  got="$(nix develop --set-env-var FOO 'BAR' --unset-env-var FOO --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test that `--unset-env-var` unsets the variable, regardless of where in the command it is.
(
  expect=''
  got="$(nix develop --set-env-var FOO 'BAR' --unset-env-var FOO --set-env-var FOO 'BLA' --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test that `--set-env-var` overwrites previously set variables.
(
  expect='BLA'
  got="$(nix develop --set-env-var FOO 'BAR' --set-env-var FOO 'BLA' --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test that `--set-env-var` overwrites previously set variables.
(
  expect='BLA'
  got="$(FOO='BAR' nix develop --set-env-var FOO 'BLA' --no-write-lock-file .#hello <<EOF
echo "\$FOO"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Test that multiple `--set-env-var` work.
(
  expect='BARFOO'
  got="$(nix develop --set-env-var FOO 'BAR' --set-env-var BAR 'FOO' --no-write-lock-file .#hello <<EOF | tr -d '\n'
echo "\$FOO"
echo "\$BAR"
EOF
)"
  [[ "$got" == "$expect" ]]
)

# Check that we throw an error when `--keep-env-var` is used without `--ignore-env`.
expectStderr 1 nix develop --keep-env-var FOO .#hello |
  grepQuiet "error: --keep-env-var does not make sense without --ignore-env"

# Check that we throw an error when `--unset-env-var` is used with `--ignore-env`.
expectStderr 1 nix develop --ignore-env --unset-env-var FOO .#hello |
  grepQuiet "error: --unset-env-var does not make sense with --ignore-env"

# Determine the bashInteractive executable.
nix build --no-write-lock-file './nixpkgs#bashInteractive' --out-link ./bash-interactive
BASH_INTERACTIVE_EXECUTABLE="$PWD/bash-interactive/bin/bash"

# Test whether `nix develop` sets `SHELL` to nixpkgs#bashInteractive shell.
[[ "$(
    SHELL=custom nix develop --no-write-lock-file .#hello <<EOF
echo "\$SHELL"
EOF
)" -ef "$BASH_INTERACTIVE_EXECUTABLE" ]]

# Test whether `nix develop` with ignore environment sets `SHELL` to nixpkgs#bashInteractive shell.
[[ "$(
    SHELL=custom nix develop --ignore-env --no-write-lock-file .#hello <<EOF
echo "\$SHELL"
EOF
)" -ef "$BASH_INTERACTIVE_EXECUTABLE" ]]

clearStore
