#!/usr/bin/env bash

# An `ssh-ng` machine that cannot be started is passed over per build, and
# errors name the machine that ran the build.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
log="$TEST_ROOT/log"
rm -rf "$cache"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

unreachable="ssh-ng://localhost?remote-program=$TEST_ROOT/no-such-program - - 1 2"
reachable="ssh-ng://localhost - - 1 1"

# A chain, so that each build is selected at load 0 and the speed factor
# picks the unreachable machine.
# shellcheck disable=SC2016
outPaths=$(timeout 200 nix-build --store "file://$cache" -j0 --no-out-link -vvv \
  --builders "$unreachable ; $reachable" -E '
  with import '"${config_nix}"';
  let a = mkDerivation { name = "inprocess-failover-ssh-ng-a"; buildCommand = "echo a > $out"; };
  in [ a (mkDerivation { name = "inprocess-failover-ssh-ng-b"; buildCommand = "cat ${a} > $out; echo b >> $out"; }) ]
' 2> "$log")

[[ $(echo "$outPaths" | wc -l) -eq 2 ]]
for outPath in $outPaths; do
    nix path-info --store "file://$cache" "$outPath"
done

[[ $(grep -c "building '.*-inprocess-failover-ssh-ng-[ab].drv' on 'ssh-ng://localhost?remote-program" "$log") -eq 2 ]]
[[ $(grep -c "cannot build on 'ssh-ng://localhost?remote-program=.*no-such-program" "$log") -eq 2 ]]
[[ $(grep -c "building '.*-inprocess-failover-ssh-ng-[ab].drv' on 'ssh-ng://localhost' instead" "$log") -eq 2 ]]
[[ $(grep -c "remote build done" "$log") -eq 2 ]]

grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "is not an ssh-ng store" "$log"
grepQuietInverse "still in flight when the worker went away" "$log"
grepQuietInverse "Nix crashed" "$log"

failLog="$TEST_ROOT/log-failing"
status=0
# shellcheck disable=SC2016
timeout 200 nix-build --store "file://$cache" -j0 --no-out-link -vvv --show-trace \
  --builders "$unreachable ; $reachable" -E '
  with import '"${config_nix}"';
  mkDerivation { name = "inprocess-failover-ssh-ng-fails"; buildCommand = "echo failing-on-purpose; exit 1"; }
' 2> "$failLog" || status=$?
[[ $status -eq 100 ]] || { cat "$failLog" >&2; fail "expected exit status 100 for a build that failed on the machine, got $status"; }
grepQuiet "cannot build on 'ssh-ng://localhost?remote-program=.*no-such-program" "$failLog"
grepQuiet "while building '.*-inprocess-failover-ssh-ng-fails.drv' on 'ssh-ng://localhost'" "$failLog"
grepQuietInverse "while building '.*-inprocess-failover-ssh-ng-fails.drv' on 'ssh-ng://localhost?remote-program" "$failLog"
grepQuietInverse "Nix crashed" "$failLog"
