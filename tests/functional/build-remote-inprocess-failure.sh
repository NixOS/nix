#!/usr/bin/env bash

# A remote build fails while its sibling is in flight on the same machine.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
log="$TEST_ROOT/log"
rm -rf "$cache" "$TEST_ROOT/sync-"*
mkdir -p "$TEST_ROOT/sync-kg" "$TEST_ROOT/sync-stop"
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; touch "$TEST_ROOT/sync-kg/release" "$TEST_ROOT/sync-stop/release"; [[ -n ${client-} ]] && kill -KILL "$client" 2> /dev/null || true; killDaemon' EXIT

cat > "$TEST_ROOT/failure.nix" <<'NIX'
{ configNix, sync, tag }:
with import configNix;
let
  waitFor = file: ''
    i=0
    while [ ! -e ${file} ]; do
      i=$((i + 1))
      if [ $i -ge 1200 ]; then exit 2; fi
      sleep 0.1
    done
  '';
in
[
  (mkDerivation {
    name = "inprocess-failure-${tag}-fail";
    buildCommand = ''
      ${waitFor "${sync}/held-pid"}
      echo failing on purpose
      exit 1
    '';
  })
  (mkDerivation {
    name = "inprocess-failure-${tag}-held";
    buildCommand = ''
      echo $$ > ${sync}/held-pid
      ${waitFor "${sync}/release"}
      echo $out > ${sync}/held-out
      echo held > $out
    '';
  })
]
NIX

startClient() {
    local tag=$1
    shift
    timeout 200 nix-build "$TEST_ROOT/failure.nix" \
      --argstr configNix "$config_nix" --argstr sync "$TEST_ROOT/sync-$tag" --argstr tag "$tag" \
      --store "file://$cache" --builders "ssh-ng://localhost - - 2 1" -j0 \
      --no-out-link -vvv "$@" 2> "$log" &
    client=$!
}

waitForClient() {
    for _ in $(seq 120); do
        kill -0 "$client" 2> /dev/null || break
        sleep 1
    done
    kill -0 "$client" 2> /dev/null && fail "the client is still running after two minutes"
    status=0
    wait "$client" || status=$?
}

sync="$TEST_ROOT/sync-kg"
startClient kg --keep-going

for _ in $(seq 120); do
    [[ -s $sync/held-pid ]] && break
    sleep 1
done
[[ -s $sync/held-pid ]] || fail "the held build did not start"
held=$(< "$sync/held-pid")

# Traced in the same event-loop turn as the goal takes in the failure.
for _ in $(seq 120); do
    grepQuiet "inprocess-failure-kg-fail.drv': remote build done" "$log" && break
    sleep 1
done
grepQuiet "inprocess-failure-kg-fail.drv': remote build done" "$log" \
    || fail "the failing build did not finish"

kill -0 "$held" 2> /dev/null || fail "the held builder died with the failing one"
kill -0 "$client" 2> /dev/null || fail "the client exited before the held build was released"
touch "$sync/release"
waitForClient

[[ $status -eq 100 ]] || fail "expected exit status 100, got $status"
grepQuietInverse "Nix crashed" "$log"
grepQuiet "builder failed with exit code 1" "$log"
[[ -s $sync/held-out ]] || fail "the held build did not finish"
nix path-info --store "file://$cache" "$(< "$sync/held-out")"
[[ $(grep -c "remote build done" "$log") -eq 2 ]]
grepQuietInverse "still in flight when the worker went away" "$log"
grepQuietInverse "starting build hook '" "$log"

sync="$TEST_ROOT/sync-stop"
startClient stop

for _ in $(seq 120); do
    [[ -s $sync/held-pid ]] && break
    sleep 1
done
[[ -s $sync/held-pid ]] || fail "the held build did not start"
held=$(< "$sync/held-pid")

# Nothing releases the held build, so the client must finish without it.
waitForClient

[[ $status -eq 100 ]] || fail "expected exit status 100, got $status"
grepQuietInverse "Nix crashed" "$log"
grepQuiet "builder failed with exit code 1" "$log"

for _ in $(seq 60); do
    kill -0 "$held" 2> /dev/null || break
    sleep 1
done
if kill -0 "$held" 2> /dev/null; then
    kill -KILL "$held" || true
    fail "the held builder is still running a minute after the client exited"
fi
[[ ! -e $sync/held-out ]] || fail "the held build finished although it was never released"

grepQuietInverse "still in flight when the worker went away" "$log"
grepQuietInverse "starting build hook '" "$log"
