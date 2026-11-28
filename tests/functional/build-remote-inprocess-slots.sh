#!/usr/bin/env bash

# A machine's `maxJobs` bounds the remote builds on it, and every slot is used.

source common.sh

export UBSAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1

TODO_NixOS
requireDaemonIsThisVersion

cache="$TEST_ROOT/cache"
log="$TEST_ROOT/log"
rm -rf "$cache" "$TEST_ROOT/sync-"*
mkdir -p "$TEST_ROOT/sync-one" "$TEST_ROOT/sync-two/inflight" "$TEST_ROOT/sync-two/done" "$TEST_ROOT/sync-three"
client=
trap '[[ $? -eq 0 ]] || { echo "--- client log ---" >&2; cat "$log" >&2; }; touch "$TEST_ROOT/sync-three/release"; if [[ -n $client ]]; then kill -KILL -- -"$client" 2> /dev/null || true; fi; killDaemon' EXIT

sync="$TEST_ROOT/sync-one"

cat > "$TEST_ROOT/slots-one.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
map (k: mkDerivation {
  name = "inprocess-slots-one-${k}";
  buildCommand = ''
    mkdir ${sync}/inflight 2> /dev/null || echo "overlap ${k}" >> ${sync}/bad
    sleep 1
    rmdir ${sync}/inflight 2> /dev/null || echo "overlap ${k}" >> ${sync}/bad
    echo ${k} > $out
  '';
}) [ "a" "b" "c" ]
NIX

outPaths=$(timeout 200 nix-build "$TEST_ROOT/slots-one.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
  --no-out-link -vvv 2> "$log")

[[ $(echo "$outPaths" | wc -l) -eq 3 ]]
for outPath in $outPaths; do
    nix path-info --store "file://$cache" "$outPath"
done
[[ ! -e $sync/bad ]] || fail "builders overlapped on a one-slot machine: $(< "$sync/bad")"
[[ $(grep -c "remote build done" "$log") -eq 3 ]]
grepQuietInverse "starting build hook '" "$log"

sync="$TEST_ROOT/sync-two"

# A worker that ran one remote build at a time would time out here.
cat > "$TEST_ROOT/slots-two.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
map (k: mkDerivation {
  name = "inprocess-slots-two-${k}";
  buildCommand = ''
    touch ${sync}/inflight/${k}
    n=$(ls ${sync}/inflight | wc -l)
    if [ $n -gt 2 ]; then echo "${k} saw $n in flight" >> ${sync}/bad; fi
    i=0
    while true; do
      if [ $(ls ${sync}/inflight | wc -l) -ge 2 ]; then echo overlap >> ${sync}/met; break; fi
      if [ $(ls ${sync}/done | wc -l) -ge 3 ]; then echo alone >> ${sync}/met; break; fi
      i=$((i + 1))
      if [ $i -ge 600 ]; then echo "${k} timed out" >> ${sync}/bad; exit 1; fi
      sleep 0.1
    done
    rm ${sync}/inflight/${k}
    touch ${sync}/done/${k}
    echo ${k} > $out
  '';
}) [ "a" "b" "c" "d" ]
NIX

outPaths=$(timeout 200 nix-build "$TEST_ROOT/slots-two.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh-ng://localhost - - 2 1" -j0 \
  --no-out-link -vvv 2> "$log")

[[ $(echo "$outPaths" | wc -l) -eq 4 ]]
for outPath in $outPaths; do
    nix path-info --store "file://$cache" "$outPath"
done
[[ ! -e $sync/bad ]] || fail "$(< "$sync/bad")"
[[ $(wc -l < "$sync/met") -eq 4 ]]
# The first two start together, so one of them sees the other in flight.
[[ $(grep -c '^overlap$' "$sync/met") -ge 1 ]] || fail "no two builders ran at once on a two-slot machine"
[[ $(grep -c "remote build done" "$log") -eq 4 ]]
grepQuietInverse "starting build hook '" "$log"

sync="$TEST_ROOT/sync-three"

# The first holds its slot until the second has been turned away twice.
cat > "$TEST_ROOT/slots-three.nix" <<'NIX'
{ configNix, sync }:
with import configNix;
map (k: mkDerivation {
  name = "inprocess-slots-three-${k}";
  buildCommand = ''
    touch ${sync}/started-${k}
    i=0
    while [ ! -e ${sync}/release ]; do
      i=$((i + 1))
      if [ $i -ge 1200 ]; then exit 1; fi
      sleep 0.1
    done
    echo ${k} > $out
  '';
}) [ "a" "b" ]
NIX

timeout 200 nix-build "$TEST_ROOT/slots-three.nix" \
  --argstr configNix "$config_nix" --argstr sync "$sync" \
  --store "file://$cache" --builders "ssh-ng://localhost - - 1 1" -j0 \
  --option build-poll-interval 1 --no-out-link -vvv 2> "$log" &
client=$!

for _ in $(seq 120); do
    [[ -e $sync/started-a || -e $sync/started-b ]] && break
    sleep 1
done
if [[ -e $sync/started-a ]]; then
    other=b
elif [[ -e $sync/started-b ]]; then
    other=a
else
    fail "neither build started"
fi

turnedAway() {
    grep -c "building derivation '.*-inprocess-slots-three-$other.drv': wait for a while" "$log" || true
}
before=$(turnedAway)
for _ in $(seq 120); do
    [[ -e $sync/started-$other ]] && break
    (( $(turnedAway) >= before + 2 )) && break
    sleep 0.5
done
[[ ! -e $sync/started-$other ]] || fail "the second build started on a one-slot machine while the first was running"
(( $(turnedAway) >= before + 2 )) || fail "the waiting build was never turned away again"

touch "$sync/release"
status=0
wait "$client" || status=$?
client=
[[ $status -eq 0 ]] || fail "nix-build failed with status $status"
[[ -e $sync/started-$other ]]
[[ $(grep -c "remote build done" "$log") -eq 2 ]]
grepQuietInverse "starting build hook '" "$log"
