#!/usr/bin/env bash

# Sixteen remote builds from one worker are in flight at once.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

n=16
cache="$TEST_ROOT/cache"
sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
rm -rf "$cache" "$sync"
mkdir -p "$sync"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; killDaemon' EXIT

# Each builder waits three minutes, as uploads to the machine run one at a time.
cat > "$TEST_ROOT/many.nix" <<'NIX'
{ configNix, sync, n }:
with import configNix;
builtins.genList (k: mkDerivation {
  name = "inprocess-many-${toString k}";
  buildCommand = ''
    echo started > ${sync}/marker-${toString k}
    i=0
    while true; do
      set -- ${sync}/marker-*
      if [ $# -ge ${toString n} ]; then break; fi
      i=$((i + 1))
      if [ $i -ge 1800 ]; then echo timeout >> ${sync}/met; exit 1; fi
      sleep 0.1
    done
    echo ok >> ${sync}/met
    echo ${toString k} > $out
  '';
}) n
NIX

outPaths=$(timeout 200 nix-build "$TEST_ROOT/many.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" --arg n "$n" \
  --store "file://$cache" --builders "ssh-ng://localhost - - $n 1" -j0 \
  --no-out-link -vvv 2> "$log")

[[ $(echo "$outPaths" | wc -l) -eq $n ]]
for outPath in $outPaths; do
    nix path-info --store "file://$cache" "$outPath"
done
[[ $(grep -c '^ok$' "$sync/met") -eq $n ]]
grepQuietInverse timeout "$sync/met"

[[ $(grep -c "remote build done" "$log") -eq $n ]]
grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "still in flight when the worker went away" "$log"
