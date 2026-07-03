#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# Reproduce llakala's pipeline: perf record -g | perf script | inferno-collapse-perf
#   ./profile.sh <nix-binary> <impure|pure> <out-basename> [callgraph]
# callgraph: dwarf (default) | fp
set -euo pipefail
BIN="$1"; MODE="$2"; OUT="$3"; CG="${4:-dwarf}"
SCR=${SCRATCH}
NPKGS=/nix/store/h6xaqhg2fsxjb0zpypx9dj7dbnznp9lx-source
BENCH="$SCR/bench"

case "$MODE" in
  impure) RUNCMD="NIX_PATH=nixpkgs=$NPKGS '$BIN' eval --impure --raw --file '$BENCH/impure_expr.nix'" ;;
  pure)   RUNCMD="'$BIN' eval --raw --option eval-cache false 'path:$BENCH#top'" ;;
esac

case "$CG" in
  dwarf) CGOPT="dwarf,16384" ;;
  fp)    CGOPT="fp" ;;
esac

eval "$RUNCMD" >/dev/null 2>&1   # warm
setarch -R taskset -c 4-7 \
  perf record -g --call-graph "$CGOPT" -F 999 -o "$OUT.perf.data" -- bash -c "$RUNCMD" >/dev/null 2>&1
perf script -i "$OUT.perf.data" 2>/dev/null | inferno-collapse-perf > "$OUT.collapsed"
inferno-flamegraph --title "$(basename "$OUT")" "$OUT.collapsed" > "$OUT.svg"
echo "=== $OUT: top 15 self-heavy leaf frames ==="
awk '{n=split($0,a," ");cnt=a[n];sub(/ [0-9]+$/,"");m=split($0,b,";");print cnt"\t"b[m]}' "$OUT.collapsed" \
  | sort -rn | head -15
echo "=== total samples ==="; awk '{n=split($0,a," ");s+=a[n]}END{print s}' "$OUT.collapsed"
