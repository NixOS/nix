#!/usr/bin/env bash

# Goals that want the build hook at once share one hook, taking turns.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

requireDaemonIsThisVersion
TODO_NixOS

clearStore

# With the default `build-hook` there is no hook process to count.
echo "build-hook = $(type -P nix) __build-remote" >> "$test_nix_conf"
restartDaemon

# The machine matches no derivation, so the hook declines each and stays.
# shellcheck disable=SC2016
nix-build --max-jobs 6 --no-out-link -vvv \
    --builders "$TEST_ROOT/nosuchstore nosuchsystem - 1 1" -E '
  with import '"${config_nix}"';
  map (n: mkDerivation {
    name = "shared-hook-${toString n}";
    buildCommand = "echo > $out";
  }) [ 1 2 3 4 5 6 ]
' 2> "$TEST_ROOT/log"

starts=$(grep -c "starting build hook '" "$TEST_ROOT/log")
[[ $starts -eq 1 ]] || fail "expected one hook start, got $starts"
