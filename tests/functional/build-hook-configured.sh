#!/usr/bin/env bash

# A configured `build-hook` that declines permanently is not started again.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS

hook="$TEST_ROOT/hook.sh"
ran="$TEST_ROOT/hook-ran"

# `exec cat` drains the input, or EPIPE on Nix's write restarts the hook.
cat > "$hook" <<EOF
#!/bin/sh
echo ran >> "$ran"
echo "# decline-permanently" >&2
exec cat > /dev/null
EOF
chmod +x "$hook"

echo "build-hook = $hook" >> "$test_nix_conf"
restartDaemon

# shellcheck disable=SC2016
nix-build --no-out-link -E '
  with import '"${config_nix}"';
  let a = mkDerivation { name = "configured-hook-a"; buildCommand = "echo a > $out"; };
  in mkDerivation { name = "configured-hook-b"; inherit a; buildCommand = "cat $a > $out"; }
'

[[ $(wc -l < "$ran") -eq 1 ]]
