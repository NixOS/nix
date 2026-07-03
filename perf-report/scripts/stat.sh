#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# Stable eval measurement harness.
#   ./stat.sh <nix-binary> <impure|pure> [reps] [label]
# Primary metric: instructions:u (frequency-independent, deterministic work).
# Secondary: cycles:u, task-clock, wall. Boost is on + governor=powersave, so
# wall/cycles are noisy; instructions are not. Pinned to physical cores 4-7
# (SMT siblings 20-23 left idle), ASLR disabled via setarch -R.
set -euo pipefail
BIN="$1"; MODE="$2"; REPS="${3:-12}"; LABEL="${4:-$MODE}"
SCR=${SCRATCH}
NPKGS=/nix/store/h6xaqhg2fsxjb0zpypx9dj7dbnznp9lx-source
BENCH="$SCR/bench"

case "$MODE" in
  impure) RUNCMD="NIX_PATH=nixpkgs=$NPKGS '$BIN' eval --impure --raw --file '$BENCH/impure_expr.nix'" ;;
  pure)   RUNCMD="'$BIN' eval --raw --option eval-cache false 'path:$BENCH#top'" ;;
  *) echo "bad mode"; exit 1 ;;
esac

# Warmup (populate OS page cache, tarball cache, reach steady freq) — excluded.
for i in 1 2 3; do eval "$RUNCMD" >/dev/null 2>&1 || { echo "WARMUP FAILED"; eval "$RUNCMD"; exit 1; }; done

echo "### $LABEL  ($BIN, mode=$MODE, reps=$REPS)"
setarch -R taskset -c 4-7 \
  perf stat -r "$REPS" -e instructions:u,cycles:u,task-clock \
    -- bash -c "$RUNCMD" >/dev/null 2> >(grep -E "instructions|cycles|task-clock|time elapsed" >&2)
