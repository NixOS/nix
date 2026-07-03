#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
# Claim: VACUUMing Nix's sqlite DBs helps. Measure reclaimed space + time on COPIES.
set -euo pipefail
WORK=${SCRATCH}
report() {
  local label="$1" db="$2"
  echo "=== $label ==="
  local before; before=$(stat -c%s "$db")
  local pc fc ps; pc=$(sqlite3 "$db" 'PRAGMA page_count;'); fc=$(sqlite3 "$db" 'PRAGMA freelist_count;'); ps=$(sqlite3 "$db" 'PRAGMA page_size;')
  echo "before: $(numfmt --to=iec $before)  page_size=$ps page_count=$pc freelist=$fc ($(numfmt --to=iec $((fc*ps))) free)"
  local t0 t1
  t0=$(date +%s.%N); sqlite3 "$db" 'VACUUM;'; t1=$(date +%s.%N)
  local after; after=$(stat -c%s "$db")
  echo "after:  $(numfmt --to=iec $after)  reclaimed=$(numfmt --to=iec $((before-after)))  vacuum_time=$(echo "$t1-$t0"|bc)s"
}

# 1. fetcher-cache-v4 (166MB) — safe user-owned copy
cp "$HOME/.cache/nix/fetcher-cache-v4.sqlite" "$WORK/fc.sqlite"
report "fetcher-cache-v4.sqlite" "$WORK/fc.sqlite"

# 2. store db.sqlite (3.6GB, world-readable) — copy only if scratch has room
FREE=$(df --output=avail -k "$WORK" | tail -1)
if [ "$FREE" -gt 8000000 ]; then
  cp /nix/var/nix/db/db.sqlite "$WORK/storedb.sqlite"
  report "store db.sqlite" "$WORK/storedb.sqlite"
  rm -f "$WORK/storedb.sqlite"
else
  echo "=== store db.sqlite: SKIPPED (insufficient scratch space: ${FREE}K) ==="
fi
rm -f "$WORK/fc.sqlite"
echo VACUUM_DONE
