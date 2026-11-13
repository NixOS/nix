#!/usr/bin/env bash

source common.sh

requireDaemonNewerThan "2.36pre"

TODO_NixOS

# Create a hook that fails for derivations containing "hook-fail".
# Note: Use $SHELL instead of /usr/bin/env bash, which is not available in Nix build sandboxes.
cat << EOF > "$TEST_ROOT/hook.sh"
#! $SHELL
drv="\$1"
case "\$drv" in
    *hook-fail*)
        exit 42
        ;;
esac
EOF
chmod +x "$TEST_ROOT/hook.sh"

echo "pre-build-hook = $TEST_ROOT/hook.sh" >> "$test_nix_conf"
restartDaemon

# shellcheck disable=SC2016
drvPass=$(nix-instantiate -E '
  with import ./config.nix;
  mkDerivation {
    name = "hook-pass";
    buildCommand = "touch $out";
  }
')

# shellcheck disable=SC2016
drvFail=$(nix-instantiate -E '
  with import ./config.nix;
  mkDerivation {
    name = "hook-fail";
    buildCommand = "touch $out";
  }
')

# Test 1: With --keep-going, a failing pre-build hook should not abort the entire build.
# The passing derivation should succeed and be valid in the store.
status=0
nix-build "$drvPass" "$drvFail" --keep-going 2> "$TEST_ROOT/log" || status=$?

# The build should report an overall failure due to hook-fail
[[ $status -ne 0 ]]

# But the passing derivation should have succeeded and its output must be valid
nix path-info "$drvPass^*"

# Verify the error log mentions the pre-build hook failure
grepQuiet "pre-build hook" "$TEST_ROOT/log"
grepQuiet "42" "$TEST_ROOT/log"

# Test 2: Unknown hook command also fails the derivation properly
cat << EOF > "$TEST_ROOT/hook-bad-cmd.sh"
#! $SHELL
echo "invalid-command"
EOF
chmod +x "$TEST_ROOT/hook-bad-cmd.sh"

# shellcheck disable=SC2016
drvBadCmd=$(nix-instantiate -E '
  with import ./config.nix;
  mkDerivation {
    name = "bad-cmd-pkg";
    buildCommand = "touch $out";
  }
')

status=0
nix-build "$drvBadCmd" --pre-build-hook "$TEST_ROOT/hook-bad-cmd.sh" 2> "$TEST_ROOT/log-bad-cmd" || status=$?

[[ $status -ne 0 ]]
grepQuiet "unknown pre-build hook command" "$TEST_ROOT/log-bad-cmd"
