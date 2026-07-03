#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# Claim: tarball-cache degrades over time; `git multi-pack-index write/repack/expire` helps.
# Non-destructive: operate on a COPY of the user's cache.
set -euo pipefail
SRC="$HOME/.cache/nix/tarball-cache-v2"
WORK=${SCRATCH}/tc-copy
rm -rf "$WORK"; cp -r "$SRC" "$WORK"; cd "$WORK"
export GIT_DIR="$WORK"

BENCH='git cat-file --batch-all-objects --batch-check --unordered >/dev/null'

packcount() { find objects/pack -maxdepth 1 -name '*.pack' 2>/dev/null | wc -l; }

echo "=== BEFORE ==="
echo "packs: $(packcount)"
echo "on-disk: $(du -sh . | cut -f1)"
hyperfine -w 2 -r 10 "$BENCH"

# xokdvium's EXACT maintenance — nothing else (no destructive `git repack -ad`,
# which would prune the cache's unreferenced objects).
echo "=== APPLY: git multi-pack-index write / repack / expire ==="
git multi-pack-index write
git multi-pack-index repack --batch-size=0
git multi-pack-index expire

echo "=== AFTER ==="
echo "packs: $(packcount)"
echo "midx: $([ -f objects/pack/multi-pack-index ] && echo yes || echo no)"
echo "on-disk: $(du -sh . | cut -f1)"
hyperfine -w 2 -r 10 "$BENCH"
echo TARBALL_DONE
