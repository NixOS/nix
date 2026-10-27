#!/usr/bin/env bash

# `max-build-log-size` stops a noisy remote build from inside the client.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
rm -rf "$cache" "$sync"
mkdir -p "$sync"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; touch "$sync/release"; [[ -n ${client-} ]] && kill -KILL "$client" 2> /dev/null || true; killDaemon' EXIT

cat > "$TEST_ROOT/log-limit.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
let
  waitFor = file: ''
    i=0
    while [ ! -e ${file} ]; do
      i=$((i + 1))
      if [ $i -ge 1200 ]; then exit 1; fi
      sleep 0.1
    done
  '';
in
[
  (mkDerivation {
    name = "inprocess-log-limit-noisy";
    buildCommand = ''
      ${waitFor "${sync}/held-pid"}
      echo $$ > ${sync}/noisy-pid
      while true; do
        echo "noisy noisy noisy noisy noisy noisy noisy noisy noisy noisy noisy"
        sleep 0.01
      done
    '';
  })
  (mkDerivation {
    name = "inprocess-log-limit-held";
    buildCommand = ''
      echo $$ > ${sync}/held-pid
      ${waitFor "${sync}/release"}
      echo $out > ${sync}/held-out
      echo held > $out
    '';
  })
]
NIX

timeout 200 nix-build "$TEST_ROOT/log-limit.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh-ng://localhost - - 2 1" -j0 \
  --keep-going --max-build-log-size 2000 \
  --no-out-link -vvv 2> "$log" &
client=$!

for _ in $(seq 120); do
    [[ -s $sync/held-pid && -s $sync/noisy-pid ]] && break
    sleep 1
done
[[ -s $sync/held-pid ]] || fail "the held build did not start"
[[ -s $sync/noisy-pid ]] || fail "the noisy build did not start"
held=$(< "$sync/held-pid")
noisy=$(< "$sync/noisy-pid")

for _ in $(seq 60); do
    kill -0 "$noisy" 2> /dev/null || break
    sleep 1
done
if kill -0 "$noisy" 2> /dev/null; then
    kill -KILL "$noisy" || true
    fail "the noisy builder is still running a minute after it started"
fi
# The client's exit would also stop the noisy build, so it must still run.
kill -0 "$client" 2> /dev/null || fail "the client exited before the held build was released"
kill -0 "$held" 2> /dev/null || fail "the held builder died with the noisy one"

touch "$sync/release"

for _ in $(seq 120); do
    kill -0 "$client" 2> /dev/null || break
    sleep 1
done
kill -0 "$client" 2> /dev/null && fail "the client is still running two minutes after the release"
status=0
wait "$client" || status=$?

# `LogLimitExceeded` sets no exit-status flag, so the status is 1.
[[ $status -eq 1 ]] || fail "expected exit status 1, got $status"
grepQuietInverse "Nix crashed" "$log"
grepQuiet "building derivation '.*-inprocess-log-limit-noisy.drv' killed after writing more than 2000 bytes of log output" "$log"

[[ -s $sync/held-out ]] || fail "the held build did not finish"
nix path-info --store "file://$cache" "$(< "$sync/held-out")"

[[ $(grep -c "remote build done" "$log") -eq 2 ]]
grepQuietInverse "still in flight when the worker went away" "$log"
grepQuietInverse "starting build hook '" "$log"
