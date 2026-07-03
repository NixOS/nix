#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# Proper test of the many-packs claim: RANDOM single-object lookup latency,
# which stresses per-pack .idx binary search (what multi-pack-index accelerates),
# unlike a bulk --batch-all-objects scan.
set -uo pipefail
SRC="$HOME/.cache/nix/tarball-cache-v2"
WORK=${SCRATCH}/tc-rnd
rm -rf "$WORK"; cp -r "$SRC" "$WORK"; cd "$WORK"; export GIT_DIR="$WORK"

# 20k random blob/tree OIDs, fixed order (seeded shuffle for reproducibility).
git cat-file --batch-all-objects --batch-check --unordered 2>/dev/null \
  | awk '$2!="commit"{print $1}' | sort -R --random-source=<(yes 42) | head -20000 > ids.txt
echo "sample OIDs: $(wc -l < ids.txt)"
BENCH='git cat-file --batch-check < ids.txt >/dev/null'

echo "=== BEFORE: $(find objects/pack -name '*.pack'|wc -l) packs ==="
hyperfine -w 3 -r 12 "$BENCH"

git multi-pack-index write 2>/dev/null
git multi-pack-index repack --batch-size=0 2>/dev/null
git multi-pack-index expire 2>/dev/null

echo "=== AFTER: $(find objects/pack -name '*.pack'|wc -l) packs + MIDX ==="
hyperfine -w 3 -r 12 "$BENCH"
echo RANDOM_DONE
