#!/usr/bin/env bash

# Two remote builds on an `ssh://` machine run at the same time. A hook
# that accepted a build belongs to that goal, so the next starts its own.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
rm -rf "$cache" "$sync"
mkdir -p "$sync"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

cat > "$TEST_ROOT/hook-concurrent.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
let
  meet = me: other: ''
    echo started > ${sync}/${me}
    i=0
    while [ ! -e ${sync}/${other} ]; do
      i=$((i + 1))
      if [ $i -ge 1200 ]; then echo timeout >> ${sync}/met; exit 1; fi
      sleep 0.1
    done
    echo ok >> ${sync}/met
    echo ${me} > $out
  '';
in
[
  (mkDerivation { name = "hook-concurrent-a"; buildCommand = meet "a" "b"; })
  (mkDerivation { name = "hook-concurrent-b"; buildCommand = meet "b" "a"; })
]
NIX

outPaths=$(timeout 200 nix-build "$TEST_ROOT/hook-concurrent.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh://localhost - - 2 1" -j0 \
  --no-out-link -vvv 2> "$log")

[[ $(echo "$outPaths" | wc -l) -eq 2 ]]
for outPath in $outPaths; do
    nix path-info --store "file://$cache" "$outPath"
done
[[ -e "$sync/a" && -e "$sync/b" ]]
[[ $(grep -c '^ok$' "$sync/met") -eq 2 ]]
grepQuietInverse timeout "$sync/met"

grepQuiet "building '.*-hook-concurrent-a.drv' on 'ssh://localhost'" "$log"
grepQuiet "building '.*-hook-concurrent-b.drv' on 'ssh://localhost'" "$log"
[[ $(grep -c "starting build hook '" "$log") -eq 2 ]]
[[ $(grep -c "hook build done" "$log") -eq 2 ]]
grepQuietInverse "remote build done" "$log"
