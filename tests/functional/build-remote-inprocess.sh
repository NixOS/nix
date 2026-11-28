#!/usr/bin/env bash

# With the default `build-hook`, an `ssh-ng://` remote build runs in-process.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion
requireSandboxSupport
requiresUnprivilegedUserNamespaces
[[ "${busybox-}" =~ busybox ]] || skipTest "no busybox"

# Avoid store dir being inside sandbox build-dir.
unset NIX_STORE_DIR

chmod -R +w "$TEST_ROOT/machine"* 2> /dev/null || true
rm -rf "$TEST_ROOT/machine"*

log="$TEST_ROOT/log"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

# shellcheck disable=SC2016
expr='
  let system = (import '"${config_nix}"').system; in
  derivation {
    name = "build-remote-inprocess";
    inherit system;
    builder = "'"$busybox"'";
    args = [ "sh" "-e" "-c" "echo hi-inprocess; echo FOO > $out" ];
  }
'

machine="ssh-ng://localhost?remote-store=$TEST_ROOT/machine1"

outPath=$(timeout 200 nix-build -E "$expr" --no-out-link -vvv --max-jobs 0 \
  --store "$TEST_ROOT/machine0" \
  --builders "$machine - - 1 1" \
  2> "$log")

grep FOO "$TEST_ROOT/machine0/$outPath"
nix path-info --store "$TEST_ROOT/machine1" "$outPath"

grepQuiet "building '.*' on 'ssh-ng://localhost" "$log"
grepQuiet "remote build done" "$log"

grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "hook build done" "$log"
grepQuietInverse "got .* remote builders" "$log"
grepQuietInverse "build hook died unexpectedly" "$log"

drvPath=$(nix-instantiate -E "$expr" --store "$TEST_ROOT/machine0")
nix log --store "$TEST_ROOT/machine0" "$drvPath" | grepQuiet hi-inprocess
