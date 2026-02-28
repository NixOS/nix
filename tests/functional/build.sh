#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

# Make sure that 'nix build' returns all outputs by default.
nix build -f multiple-outputs.nix --json a b --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.out | match(".*multiple-outputs-b"))))
'

# Test output selection using the '^' syntax.
nix build -f multiple-outputs.nix --json a^first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first"]))
'

nix build -f multiple-outputs.nix --json a^second,first --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

nix build -f multiple-outputs.nix --json 'a^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs | keys == ["first", "second"]))
'

# Test that 'outputsToInstall' is respected by default.
nix build -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b"]))
'

# Tests that we can handle empty 'outputsToInstall' (assuming that default
# output "out" exists).
nix build -f multiple-outputs.nix --json nothing-to-install --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*nothing-to-install.drv")) and
    (.outputs | keys == ["out"]))
'

# But not when it's overriden.
nix build -f multiple-outputs.nix --json e^a_a --no-link
nix build -f multiple-outputs.nix --json e^a_a --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a"]))
'

nix build -f multiple-outputs.nix --json 'e^*' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b", "c"]))
'

# test buidling from non-drv attr path

nix build -f multiple-outputs.nix --json 'e.a_a.outPath' --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a"]))
'

# Illegal type of string context
expectStderr 1 nix build -f multiple-outputs.nix 'e.a_a.drvPath' \
  | grepQuiet "has a context which refers to a complete source and binary closure."

# No string context
expectStderr 1 nix build --expr '""' --no-link \
  | grepQuiet "has 0 entries in its context. It should only have exactly one entry"

# Too much string context
# shellcheck disable=SC2016 # The ${} in this is Nix, not shell
expectStderr 1 nix build --impure --expr 'with (import ./multiple-outputs.nix).e.a_a; "${drvPath}${outPath}"' --no-link \
  | grepQuiet "has 2 entries in its context. It should only have exactly one entry"

nix build --impure --json --expr 'builtins.unsafeDiscardOutputDependency (import ./multiple-outputs.nix).e.a_a.drvPath' --no-link | jq --exit-status '
  (.[0] | match(".*multiple-outputs-e.drv"))
'

# Test building from raw store path to drv not expression.

drv=$(nix eval -f multiple-outputs.nix --raw a.drvPath)
if nix build "$drv^not-an-output" --no-link --json; then
    fail "'not-an-output' should fail to build"
fi

if nix build "$drv^" --no-link --json; then
    fail "'empty outputs list' should fail to build"
fi

if nix build "$drv^*nope" --no-link --json; then
    fail "'* must be entire string' should fail to build"
fi

nix build "$drv^first" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.first | match(".*multiple-outputs-a-first")) and
      (has("second") | not)))
'

nix build "$drv^first,second" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
'

nix build "$drv^*" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
'

# Make sure that `--impure` works (regression test for https://github.com/NixOS/nix/issues/6488)
nix build --impure -f multiple-outputs.nix --json e --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b"]))
'

# Make sure that the 3 types of aliases work
# BaseSettings<T>, BaseSettings<bool>, and BaseSettings<SandboxMode>.
nix build --impure -f multiple-outputs.nix --json e --no-link \
    --build-max-jobs 3 \
    --gc-keep-outputs \
    --build-use-sandbox | \
    jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-e.drv")) and
    (.outputs | keys == ["a_a", "b"]))
'

# Make sure that `--stdin` works and does not apply any defaults
printf "" | nix build --no-link --stdin --json | jq --exit-status '. == []'
printf "%s\n" "$drv^*" | nix build --no-link --stdin --json | jq --exit-status '.[0]|has("drvPath")'

# --keep-going and FOD
if isDaemonNewer "2.34pre"; then
    # With the fix, cancelled goals are not reported as failures.
    # Use -j1 so only x1 starts and fails; x2, x3, x4 are cancelled.
    out="$(nix build -f fod-failing.nix -j1 -L 2>&1)" && status=0 || status=$?
    test "$status" = 1
    # Only the hash mismatch error for x1. Cancelled goals not reported.
    test "$(<<<"$out" grep -cE '^error:')" = 1
    # Regression test: error messages should not be empty (end with just "failed:")
    <<<"$out" grepQuietInverse -E "^error:.*failed: *$"
else
    out="$(nix build -f fod-failing.nix -L 2>&1)" && status=0 || status=$?
    test "$status" = 1
    # At minimum, check that x1 is reported as failing
    <<<"$out" grepQuiet -E "error:.*-x1"
fi
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x1\\.drv'"
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x3\\.drv'"
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x2\\.drv'"

out="$(nix build -f fod-failing.nix -L x1 x2 x3 --keep-going 2>&1)" && status=0 || status=$?
test "$status" = 1
# three "hash mismatch" errors - for each failing fod, one "build of ... failed"
test "$(<<<"$out" grep -cE '^error:')" = 4
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x1\\.drv'"
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x3\\.drv'"
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x2\\.drv'"
<<<"$out" grepQuiet -E "error: build of '.*-x[1-3]\\.drv\\^out', '.*-x[1-3]\\.drv\\^out', '.*-x[1-3]\\.drv\\^out' failed"

out="$(nix build -f fod-failing.nix -L x4 2>&1)" && status=0 || status=$?
test "$status" = 1
# Precise number of errors depends on daemon version / goal refactorings
(( "$(<<<"$out" grep -cE '^error:')" >= 2 ))

if isDaemonNewer "2.31"; then
    <<<"$out" grepQuiet -E "error: Cannot build '.*-x4\\.drv'"
    <<<"$out" grepQuiet -E "Reason: 1 dependency failed."
elif isDaemonNewer "2.29pre"; then
    <<<"$out" grepQuiet -E "error: Cannot build '.*-x4\\.drv'"
    <<<"$out" grepQuiet -E "Reason: 1 dependency failed."
    <<<"$out" grepQuiet -E "Build failed due to failed dependency"
elif ! isDaemonNewer "2.0"; then
    # Observed with Nix 1.11.16: new client with very old daemon still produces new-style messages.
    # TODO: if the `else` branch (2.0 to 2.29pre) also produces new-style messages, merge these branches.
    <<<"$out" grepQuiet -E "error: Cannot build '.*-x4\\.drv'"
    <<<"$out" grepQuiet -E "Reason: 1 dependency failed."
else
    <<<"$out" grepQuiet -E "error: 1 dependencies of derivation '.*-x4\\.drv' failed to build"
fi
# Either x2 or x3 could have failed, x4 depends on both symmetrically
<<<"$out" grepQuiet -E "hash mismatch in fixed-output derivation '.*-x[23]\\.drv'"

out="$(nix build -f fod-failing.nix -L x4 --keep-going 2>&1)" && status=0 || status=$?
test "$status" = 1
# Precise number of errors depends on daemon version / goal refactorings
(( "$(<<<"$out" grep -cE '^error:')" >= 3 ))
if isDaemonNewer "2.29pre"; then
    <<<"$out" grepQuiet -E "error: Cannot build '.*-x4\\.drv'"
    <<<"$out" grepQuiet -E "Reason: 2 dependencies failed."
elif ! isDaemonNewer "2.0"; then
    # Observed with Nix 1.11.16: new client with very old daemon still produces new-style messages.
    # TODO: if the `else` branch (2.0 to 2.29pre) also produces new-style messages, merge these branches.
    <<<"$out" grepQuiet -E "error: Cannot build '.*-x4\\.drv'"
    <<<"$out" grepQuiet -E "Reason: 2 dependencies failed."
else
    <<<"$out" grepQuiet -E "error: 2 dependencies of derivation '.*-x4\\.drv' failed to build"
fi
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x3\\.drv'"
<<<"$out" grepQuiet -vE "hash mismatch in fixed-output derivation '.*-x2\\.drv'"

# Regression test: cancelled builds should not be reported as failures
# When fast-fail fails, slow and depends-on-slow are cancelled (not failed).
# Only fast-fail should be reported as a failure.
# Uses fifo for synchronization to ensure deterministic behavior.
# Requires -j2 so slow and fast-fail run concurrently (fifo deadlocks if serialized).
if isDaemonNewer "2.34pre" && canUseSandbox; then
    fifoDir="$TEST_ROOT/cancelled-builds-fifo"
    mkdir -p "$fifoDir"
    mkfifo "$fifoDir/fifo"
    chmod a+rw "$fifoDir/fifo"
    # When using a separate test store, we need sandbox-paths to access
    # the system store (where bash/coreutils live). On NixOS, the test
    # uses the system store directly, so this isn't needed (and would
    # conflict with input paths).
    sandboxPathsArg=()
    if ! isTestOnNixOS; then
        sandboxPathsArg=(--option sandbox-paths "/nix/store")
    fi
    out="$(nix flake check ./cancelled-builds --impure -L -j2 \
        --option sandbox true \
        "${sandboxPathsArg[@]}" \
        --option sandbox-build-dir /build-tmp \
        --option extra-sandbox-paths "/cancelled-builds-fifo=$fifoDir" \
        2>&1)" && status=0 || status=$?
    rm -rf "$fifoDir"
    test "$status" = 1
    # The error should be for fast-fail, not for cancelled goals
    <<<"$out" grepQuiet -E "Cannot build.*fast-fail"
    # Cancelled goals should NOT appear in error messages (but may appear in "will be built" list)
    <<<"$out" grepQuietInverse -E "^error:.*slow"
    <<<"$out" grepQuietInverse -E "^error:.*depends-on-slow"
    <<<"$out" grepQuietInverse -E "^error:.*depends-on-fail"
    # Error messages should not be empty (end with just "failed:")
    <<<"$out" grepQuietInverse -E "^error:.*failed: *$"

    # Test that nix build follows the same rules (uses a slightly different code path)
    mkdir -p "$fifoDir"
    mkfifo "$fifoDir/fifo"
    chmod a+rw "$fifoDir/fifo"
    sandboxPathsArg=()
    if ! isTestOnNixOS; then
        sandboxPathsArg=(--option sandbox-paths "/nix/store")
    fi
    system=$(nix eval --raw --impure --expr builtins.currentSystem)
    out="$(nix build --impure -L -j2 \
        --option sandbox true \
        "${sandboxPathsArg[@]}" \
        --option sandbox-build-dir /build-tmp \
        --option extra-sandbox-paths "/cancelled-builds-fifo=$fifoDir" \
        "./cancelled-builds#checks.$system.slow" \
        "./cancelled-builds#checks.$system.depends-on-slow" \
        "./cancelled-builds#checks.$system.fast-fail" \
        "./cancelled-builds#checks.$system.depends-on-fail" \
        2>&1)" && status=0 || status=$?
    rm -rf "$fifoDir"
    test "$status" = 1
    # The error should be for fast-fail, not for cancelled goals
    <<<"$out" grepQuiet -E "Cannot build.*fast-fail"
    # Cancelled goals should NOT appear in error messages
    <<<"$out" grepQuietInverse -E "^error:.*slow"
    <<<"$out" grepQuietInverse -E "^error:.*depends-on-slow"
    <<<"$out" grepQuietInverse -E "^error:.*depends-on-fail"
    # Error messages should not be empty (end with just "failed:")
    <<<"$out" grepQuietInverse -E "^error:.*failed: *$"
fi

# https://github.com/NixOS/nix/issues/14883
# When max-jobs=0 and no remote builders, the error should say
# "local builds are disabled" instead of the misleading
# "required system or feature not available".
if isDaemonNewer "2.34pre"; then
    expectStderr 1 nix build --impure --max-jobs 0 --expr \
      'derivation { name = "test-maxjobs"; builder = "/bin/sh"; args = ["-c" "exit 0"]; system = builtins.currentSystem; }' \
      --no-link \
      | grepQuiet "local builds are disabled"
fi
