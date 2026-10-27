#!/usr/bin/env bash

# Killing the machine's daemon mid-build fails the build without a hang or crash.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
sync="$TEST_ROOT/sync"
log="$TEST_ROOT/log"
wrapper="$TEST_ROOT/machine-daemon"
rm -rf "$cache" "$sync"
mkdir -p "$sync"
# The builder outlives its daemon, so let it finish.
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; touch "$sync/release"; [[ -n ${client-} ]] && kill -KILL "$client" 2> /dev/null || true; killDaemon' EXIT

cat > "$wrapper" <<EOF
#!/bin/sh
echo \$\$ >> "$sync/machine-pids"
exec nix-daemon "\$@"
EOF
chmod +x "$wrapper"

# shellcheck disable=SC2016
timeout 200 nix-build --store "file://$cache" -j0 --no-out-link -vvv \
    --builders "ssh-ng://localhost?remote-program=$wrapper - - 1 1" -E '
  with import '"${config_nix}"';
  mkDerivation {
    name = "inprocess-machine-dies";
    buildCommand = "
      echo $$ > '"$sync"'/builder-pid
      i=0
      while [ ! -e '"$sync"'/release ]; do
        i=$((i + 1))
        if [ $i -ge 1200 ]; then exit 1; fi
        sleep 0.1
      done
      echo done > $out
    ";
  }
' 2> "$log" &
client=$!

for _ in $(seq 120); do
    [[ -s $sync/builder-pid ]] && break
    sleep 1
done
[[ -s $sync/builder-pid ]] || fail "the remote build did not start"
[[ -s $sync/machine-pids ]] || fail "the machine's daemon was not started through the wrapper"

while read -r pid; do
    kill -KILL "$pid" 2> /dev/null || true
done < "$sync/machine-pids"

for _ in $(seq 60); do
    kill -0 "$client" 2> /dev/null || break
    sleep 1
done
if kill -0 "$client" 2> /dev/null; then
    kill -TERM "$client" || true
    fail "the client is still running a minute after the machine went away"
fi
status=0
wait "$client" || status=$?

# `MiscFailure` sets no exit-status flag, so the status is 1.
[[ $status -eq 1 ]] || fail "expected exit status 1, got $status"
grepQuietInverse "Nix crashed" "$log"

grepQuiet "Cannot build '.*-inprocess-machine-dies.drv' on 'ssh-ng://localhost" "$log"
grepQuiet "Nix daemon disconnected unexpectedly" "$log"

grepQuiet "remote build done" "$log"
grepQuietInverse "starting build hook '" "$log"
grepQuietInverse "still in flight when the worker went away" "$log"
