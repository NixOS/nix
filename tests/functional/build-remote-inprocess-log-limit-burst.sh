#!/usr/bin/env bash

# `max-build-log-size` holds when a remote build's log arrives in one burst.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
rm -rf "$cache" "$sync"
mkdir -p "$sync"
client=
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; if [[ -n $client ]]; then kill -CONT "$client" 2> /dev/null || true; kill -KILL "$client" 2> /dev/null || true; fi; if [[ -s $sync/noisy-pid ]]; then kill -KILL "$(< "$sync/noisy-pid")" 2> /dev/null || true; fi; killDaemon' EXIT

cat > "$TEST_ROOT/burst.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
mkDerivation {
  name = "inprocess-log-limit-burst";
  buildCommand = ''
    echo $$ > ${sync}/noisy-pid
    i=0
    while [ ! -e ${sync}/go ]; do
      i=$((i + 1))
      if [ $i -ge 1200 ]; then exit 1; fi
      sleep 0.1
    done
    i=0
    while true; do
      echo "noisy noisy noisy noisy noisy noisy noisy noisy noisy noisy noisy"
      i=$((i + 1))
      echo $i > ${sync}/count
    done
  '';
}
NIX

nix-build "$TEST_ROOT/burst.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
  --max-build-log-size 2000 --no-out-link -vvv 2> "$log" &
client=$!

for _ in $(seq 120); do
    [[ -s $sync/noisy-pid ]] && break
    sleep 1
done
[[ -s $sync/noisy-pid ]] || fail "the build did not start"
noisy=$(< "$sync/noisy-pid")

# Stop the client until the pipes fill and the builder blocks, so that its
# next read takes a whole buffer of lines and crosses the limit part-way.
kill -STOP "$client"
touch "$sync/go"
prev=
cur=
for _ in $(seq 60); do
    sleep 2
    cur=$(cat "$sync/count" 2> /dev/null || true)
    [[ -n $cur && $cur == "$prev" ]] && break
    prev=$cur
done
[[ -n $cur && $cur == "$prev" ]] || fail "the builder never blocked"
kill -CONT "$client"

for _ in $(seq 60); do
    kill -0 "$noisy" 2> /dev/null || break
    sleep 1
done
kill -0 "$noisy" 2> /dev/null && fail "the noisy builder is still running a minute after the client resumed"

for _ in $(seq 60); do
    kill -0 "$client" 2> /dev/null || break
    sleep 1
done
status=0
wait "$client" || status=$?
client=
[[ $status -eq 1 ]] || fail "expected exit status 1, got $status"
grepQuiet "killed after writing more than 2000 bytes of log output" "$log"

# Lines are 66 bytes, so the 31st crosses the limit and the rest are dropped.
n=$(grep -c '^noisy noisy' "$log" || true)
(( n >= 1 && n <= 40 )) || fail "expected the lines up to the limit (31), got $n"

grepQuietInverse "still in flight when the worker went away" "$log"
grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "Nix crashed" "$log"
