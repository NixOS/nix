#!/usr/bin/env bash

source common.sh

TODO_NixOS # auto-optimise-store is a restricted setting for non-trusted users

clearStoreIfPossible

# Build three derivations: foo1 and foo2 have identical contents (so
# deduplication kicks in), foo3 differs.
# shellcheck disable=SC2016
outPath1=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)
# shellcheck disable=SC2016
outPath2=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)
# shellcheck disable=SC2016
outPath3=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo unique > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)

# Make sure deduplication actually happened so the test is exercising
# what it claims to exercise.
inode1=$(stat --format=%i "$outPath1"/foo)
inode2=$(stat --format=%i "$outPath2"/foo)
[[ "$inode1" = "$inode2" ]] || fail "foo1/foo and foo2/foo are not hard-linked; auto-optimise did not run"

# `nix store stats --json` should report pathCount, dedup,
# predictedDedup, and fullWalk for a populated local store.

stats_json=$(nix store stats --json)

[[ "$(echo "$stats_json" | jq -r '.available')" = "true" ]] || fail "stats not available"
[[ "$(echo "$stats_json" | jq 'has("dedup")')" = "true" ]] || fail "stats: missing dedup section"
[[ "$(echo "$stats_json" | jq 'has("predictedDedup")')" = "true" ]] || fail "stats: missing predictedDedup section"
[[ "$(echo "$stats_json" | jq 'has("fullWalk")')" = "true" ]] || fail "stats: missing fullWalk section"
[[ "$(echo "$stats_json" | jq 'has("narSizeHistogram")')" = "false" ]] || fail "stats (no --histograms) unexpectedly included narSizeHistogram"
[[ "$(echo "$stats_json" | jq '.dedup | has("sizeHistogram")')" = "false" ]] || fail "stats (no --histograms) unexpectedly included .links sizeHistogram"

path_count=$(echo "$stats_json" | jq -r '.pathCount')
(( path_count >= 3 )) || fail "pathCount $path_count < 3"

# Verify totalNarSize is reported in JSON (callers may want it) even
# though the human output deliberately suppresses the misleading number.
nar_total=$(echo "$stats_json" | jq -r '.totalNarSize')
(( nar_total > 0 )) || fail "totalNarSize is 0"

# Dedup section.
links_count=$(echo "$stats_json" | jq -r '.dedup.linksFileCount')
unique_bytes=$(echo "$stats_json" | jq -r '.dedup.uniqueBytes')
unique_disk_bytes=$(echo "$stats_json" | jq -r '.dedup.uniqueDiskBytes')
dedup_bytes=$(echo "$stats_json" | jq -r '.dedup.dedupBytes')
dedup_disk_bytes=$(echo "$stats_json" | jq -r '.dedup.dedupDiskBytes')
deduped_count=$(echo "$stats_json" | jq -r '.dedup.dedupedFileCount')
inodes_saved=$(echo "$stats_json" | jq -r '.dedup.inodesSaved')
predicted_files=$(echo "$stats_json" | jq -r '.predictedDedup.filesLinkable')
predicted_bytes=$(echo "$stats_json" | jq -r '.predictedDedup.bytesLinkable')

(( links_count > 0 )) || fail "linksFileCount is 0"
(( unique_bytes > 0 )) || fail "uniqueBytes is 0"
(( unique_disk_bytes >= unique_bytes )) || fail "uniqueDiskBytes ($unique_disk_bytes) < uniqueBytes ($unique_bytes)"
(( deduped_count >= 1 )) || fail "dedupedFileCount is 0 despite known duplicate"
(( inodes_saved >= 1 )) || fail "inodesSaved is 0 despite known duplicate"
(( dedup_bytes >= 1 )) || fail "dedupBytes is 0 despite known duplicate"
(( dedup_disk_bytes >= dedup_bytes )) || fail "dedupDiskBytes ($dedup_disk_bytes) < dedupBytes ($dedup_bytes)"
(( predicted_files == 0 )) || fail "predictedDedup.filesLinkable $predicted_files != 0 on an already-optimised store"
(( predicted_bytes == 0 )) || fail "predictedDedup.bytesLinkable $predicted_bytes != 0 on an already-optimised store"

# fullWalk section.
total_disk=$(echo "$stats_json" | jq -r '.fullWalk.totalDiskBytes')
total_inodes=$(echo "$stats_json" | jq -r '.fullWalk.totalInodes')
file_inodes=$(echo "$stats_json" | jq -r '.fullWalk.fileInodes')
dir_inodes=$(echo "$stats_json" | jq -r '.fullWalk.dirInodes')
sym_inodes=$(echo "$stats_json" | jq -r '.fullWalk.symlinkInodes')

(( total_disk > 0 )) || fail "totalDiskBytes is 0"
expected_total=$(( file_inodes + dir_inodes + sym_inodes ))
(( total_inodes == expected_total )) || fail "totalInodes ($total_inodes) != file+dir+sym ($expected_total)"
(( file_inodes > 0 )) || fail "fileInodes is 0"
(( dir_inodes > 0 )) || fail "dirInodes is 0 (store root and .links should be counted)"

# Cross-check: totalDiskBytes (every reachable inode) >= uniqueDiskBytes
# (just the `.links/` entries). The store-directory walk also covers
# directories and symlinks, so >= is the expected relation.
(( total_disk >= unique_disk_bytes )) || fail "totalDiskBytes ($total_disk) < uniqueDiskBytes ($unique_disk_bytes)"

# Cross-check: totalNarSize against `sum(narSize) from ValidPaths`. The JSON
# field is the direct mirror of that aggregate.
if type -p sqlite3 > /dev/null; then
    sql_nar_total=$(sqlite3 "$NIX_STATE_DIR/db/db.sqlite" 'select coalesce(sum(narSize), 0) from ValidPaths')
    (( nar_total == sql_nar_total )) || \
        fail "totalNarSize ($nar_total) != sum(narSize) from ValidPaths ($sql_nar_total)"
fi

# Cross-check: fullWalk.totalDiskBytes against `du`. `du` (default
# behaviour) counts hard-linked inodes once and uses st_blocks*512,
# which is exactly what walkSubtree credits.
du_total=$(du -s -B1 "$NIX_STORE_DIR" | cut -f1)
(( total_disk == du_total )) || \
    fail "totalDiskBytes ($total_disk) != du -sB1 $NIX_STORE_DIR ($du_total)"

# --- fullWalk must count garbage outside any valid path.
# Drop a regular file at the top of the store; re-run stats and check that
# fileInodes grew by exactly 1.

garbage_path="$NIX_STORE_DIR/garbage-stats-test"
echo "not a valid path" > "$garbage_path"
trap 'rm -f "$garbage_path"' EXIT

garbage_json=$(nix store stats --json)
garbage_files=$(echo "$garbage_json" | jq -r '.fullWalk.fileInodes')
(( garbage_files == file_inodes + 1 )) || \
    fail "garbage file not counted by fullWalk: baseline=$file_inodes after-garbage=$garbage_files"

rm -f "$garbage_path"
trap - EXIT

# --- --histograms adds NAR-size + .links-size distributions.

hist_json=$(nix store stats --json --histograms)
[[ "$(echo "$hist_json" | jq 'has("narSizeHistogram")')" = "true" ]] || fail "histograms: missing narSizeHistogram"
[[ "$(echo "$hist_json" | jq '.dedup | has("sizeHistogram")')" = "true" ]] || fail "histograms: missing .links/ sizeHistogram"
hist_sum=$(echo "$hist_json" | jq '[.narSizeHistogram[].count] | add')
(( hist_sum == path_count )) || fail "NAR histogram sums to $hist_sum, want $path_count"
links_hist_sum=$(echo "$hist_json" | jq '[.dedup.sizeHistogram[].count] | add')
(( links_hist_sum == links_count )) || fail ".links/ histogram sums to $links_hist_sum, want $links_count"

# Cross-check: the histogram-path SQL query goes via row iteration
# (different code path from the non-histogram aggregate), so verify
# totalNarSize independently under --histograms too.
if type -p sqlite3 > /dev/null; then
    hist_nar_total=$(echo "$hist_json" | jq -r '.totalNarSize')
    sql_nar_total=$(sqlite3 "$NIX_STATE_DIR/db/db.sqlite" 'select coalesce(sum(narSize), 0) from ValidPaths')
    (( hist_nar_total == sql_nar_total )) || \
        fail "--histograms totalNarSize ($hist_nar_total) != sum(narSize) from ValidPaths ($sql_nar_total)"
fi

# --- nix store optimise --dry-run: reports current + predicted.

optimise_dry_run=$(nix store optimise --dry-run 2>&1)
echo "$optimise_dry_run" | grep -q 'Currently saved by hard-linking:' || fail "optimise --dry-run: missing current-savings header: $optimise_dry_run"
echo "$optimise_dry_run" | grep -q 'If `nix store optimise` were run now:' || fail "optimise --dry-run: missing prediction header: $optimise_dry_run"
echo "$optimise_dry_run" | grep -qE 'Additional files linkable:[[:space:]]+0' || fail "optimise --dry-run: non-zero predicted files on already-optimised store: $optimise_dry_run"

# --- human-readable smoke checks.
# The default output should NOT print "Total NAR size" — that number
# is misleading for stores with heavy hard-link deduplication. Disk
# usage and current hard-linking savings lead instead.

human=$(nix store stats --histograms 2>&1)
echo "$human" | grep -q 'Valid paths:' || fail "human output missing 'Valid paths:'"
echo "$human" | grep -q 'Disk usage:' || fail "human output missing 'Disk usage:'"
echo "$human" | grep -q 'Saved by hard-linking:' || fail "human output missing 'Saved by hard-linking:'"
echo "$human" | grep -q 'Additional savings available:' || fail "human output missing 'Additional savings available:'"
echo "$human" | grep -q 'Store breakdown:' || fail "human output missing 'Store breakdown:'"
echo "$human" | grep -q 'NAR size distribution:' || fail "human output missing 'NAR size distribution:'"
echo "$human" | grep -q '\.links/ size distribution:' || fail "human output missing '.links/ size distribution:'"
echo "$human" | grep -vq 'Total NAR size:' || fail "human output still has misleading 'Total NAR size:' line"

# --- Empty store: every count rolls up to zero.
# clearStore wipes $NIX_STORE_DIR (so .links/ is gone) and $NIX_STATE_DIR
# (so the SQLite DB starts fresh too); the next `nix` invocation lazily
# recreates the DB.

clearStore

empty_json=$(nix store stats --json)
[[ "$(echo "$empty_json" | jq -r '.pathCount')" = "0" ]] || fail "empty store: pathCount != 0"
[[ "$(echo "$empty_json" | jq -r '.totalNarSize')" = "0" ]] || fail "empty store: totalNarSize != 0"
[[ "$(echo "$empty_json" | jq -r '.dedup.linksFileCount')" = "0" ]] || fail "empty store: dedup.linksFileCount != 0"
[[ "$(echo "$empty_json" | jq -r '.dedup.dedupBytes')" = "0" ]] || fail "empty store: dedup.dedupBytes != 0"
[[ "$(echo "$empty_json" | jq -r '.predictedDedup.filesLinkable')" = "0" ]] || fail "empty store: predictedDedup.filesLinkable != 0"
[[ "$(echo "$empty_json" | jq -r '.predictedDedup.bytesLinkable')" = "0" ]] || fail "empty store: predictedDedup.bytesLinkable != 0"
[[ "$(echo "$empty_json" | jq -r '.fullWalk.fileInodes')" = "0" ]] || fail "empty store: fullWalk.fileInodes != 0"
[[ "$(echo "$empty_json" | jq -r '.fullWalk.symlinkInodes')" = "0" ]] || fail "empty store: fullWalk.symlinkInodes != 0"
empty_dirs=$(echo "$empty_json" | jq -r '.fullWalk.dirInodes')
(( empty_dirs >= 1 )) || fail "empty store: dirInodes ($empty_dirs) should count the store root"

# --- Store with empty .links/: build without --auto-optimise-store, so
# `.links/` (created by LocalStore init) stays empty. Stats must report
# dedup zeros but non-zero predictedDedup, since every file in the store
# is unique on disk and would be linkable.
# Note: this exercises the `scanLinks` happy path on an empty directory,
# not the ENOENT branch — `.links/` is recreated by store init on every
# nix invocation, so a truly missing `.links/` is only reachable as a
# transient race and is not testable here.

clearStore

# Two derivations with identical content: the second occurrence of the
# shared hash is what makes `predictedDedup` non-zero (one file would
# stay as the canonical `.links/<hash>`, the other would be replaced
# with a hard link to it).
# shellcheck disable=SC2016
unopt_out1=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "unopt1"; builder = builtins.toFile "builder" "mkdir $out; echo unopt-payload > $out/foo"; }' | nix-build - --no-out-link)
# shellcheck disable=SC2016
unopt_out2=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "unopt2"; builder = builtins.toFile "builder" "mkdir $out; echo unopt-payload > $out/foo"; }' | nix-build - --no-out-link)

# Sanity: nothing should have been written to .links/ since we didn't ask for
# auto-optimise.
empty_links_count=$(find "$NIX_STORE_DIR/.links" -mindepth 1 -maxdepth 1 | wc -l)
(( empty_links_count == 0 )) || fail "empty-.links setup: .links/ has $empty_links_count entries, expected 0"

# And the two outputs really hold separate-but-identical files (i.e.
# auto-optimise did not silently link them).
inode_a=$(stat --format=%i "$unopt_out1"/foo)
inode_b=$(stat --format=%i "$unopt_out2"/foo)
[[ "$inode_a" != "$inode_b" ]] || fail "empty-.links setup: outputs are already hard-linked"

unopt_json=$(nix store stats --json)
[[ "$(echo "$unopt_json" | jq -r '.dedup.linksFileCount')" = "0" ]] || fail "empty-.links: dedup.linksFileCount != 0"
[[ "$(echo "$unopt_json" | jq -r '.dedup.dedupBytes')" = "0" ]] || fail "empty-.links: dedup.dedupBytes != 0"
unopt_predicted_files=$(echo "$unopt_json" | jq -r '.predictedDedup.filesLinkable')
(( unopt_predicted_files >= 1 )) || fail "empty-.links: predictedDedup.filesLinkable ($unopt_predicted_files) should be >= 1 for a known duplicate"

# --- Known-bucket placement: a path of known NAR size N must contribute
# to the histogram bucket floor(log2(N)).
# Use sqlite3 to read narSize so we are not coupled to nix path-info's
# JSON shape across versions.

if type -p sqlite3 > /dev/null; then
    nar_size=$(sqlite3 "$NIX_STATE_DIR/db/db.sqlite" "select narSize from ValidPaths where path = '$unopt_out1'")
    expected_bucket=$(awk -v n="$nar_size" 'BEGIN { if (n == 0) { print 0; exit } b = 0; while (n > 1) { n = int(n/2); b++ } print b }')
    bucket_hist_json=$(nix store stats --json --histograms)
    bucket_count=$(echo "$bucket_hist_json" | jq --argjson b "$expected_bucket" '[.narSizeHistogram[] | select(.bucket == $b) | .count] | add // 0')
    (( bucket_count >= 1 )) || \
        fail "NAR bucket placement: path of size $nar_size expected in bucket $expected_bucket, got count=$bucket_count"
fi

# --- Invariant: predictedDedup counts each inode at most once.
# Two store paths with identical content but distinct inodes are a
# real optimise opportunity (predictor should count 1). If they are
# then hard-linked together outside `.links/`, the second occurrence
# becomes a no-op for an optimise pass (wet-mode's `nlink > 1 &&
# inodeHash.count` guard fires after the first sighting registers
# the inode), so the predictor must stop counting it.

clearStore

# shellcheck disable=SC2016
shared_out1=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "shared-a"; builder = builtins.toFile "builder" "mkdir $out; echo shared-payload > $out/foo"; }' | nix-build - --no-out-link)
# shellcheck disable=SC2016
shared_out2=$(echo 'with import '"${config_nix}"'; mkDerivation { name = "shared-b"; builder = builtins.toFile "builder" "mkdir $out; echo shared-payload > $out/foo"; }' | nix-build - --no-out-link)

inode_a=$(stat --format=%i "$shared_out1"/foo)
inode_b=$(stat --format=%i "$shared_out2"/foo)
[[ "$inode_a" != "$inode_b" ]] || fail "over-count test: outputs unexpectedly share an inode at build time"

# Baseline: distinct inodes, same content → one would-be-linked.
pre=$(nix store stats --json | jq -r '.predictedDedup.filesLinkable')
(( pre >= 1 )) || fail "over-count baseline: expected predictedDedup.filesLinkable >= 1, got $pre"

# Hardlink the two outputs together outside `.links/`.
chmod -R +w "$shared_out1" "$shared_out2"
ln -f "$shared_out1"/foo "$shared_out2"/foo
post_inode_a=$(stat --format=%i "$shared_out1"/foo)
post_inode_b=$(stat --format=%i "$shared_out2"/foo)
[[ "$post_inode_a" = "$post_inode_b" ]] || fail "over-count test: ln -f did not share inode"

# Same content + same inode means an optimise pass would do nothing
# (the second occurrence is short-circuited). predictedDedup must
# reflect that — strictly less than the baseline.
post=$(nix store stats --json | jq -r '.predictedDedup.filesLinkable')
(( post < pre )) || fail "over-count: shared-inode siblings still counted (pre=$pre post=$post)"
