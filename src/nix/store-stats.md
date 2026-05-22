R""(

# Examples

* Default summary — `pathCount`, disk usage, current hard-linking
  savings, and predicted additional savings:

  ```console
  # nix store stats
  ```

* Add power-of-two size distributions for NAR archives and `.links/`
  contents:

  ```console
  # nix store stats --histograms
  ```

* JSON for machine consumption:

  ```console
  # nix store stats --json
  ```

# Description

Summary statistics for the contents of a Nix store. One walk of the
store directory produces every output field:

* the `.links/` scan yields the current hard-linking picture;
* the recursive walk credits on-disk byte totals and inode counts;
* hashing every unoptimised file under valid paths yields the
  prediction of what a fresh `nix store optimise` would save.

## Default human output

* **Store URL** — the store identifier.
* **Valid paths** — number of paths in the store database.
* **Disk usage** — actual bytes on disk (`du --block-size=1`-style),
  with hard-linked inodes counted once.
* **Saved by hard-linking** — bytes that auto-optimise has already
  saved, the count of duplicate copies eliminated, and the count
  of unique contents (entries in `.links/`).
* **Additional savings available** — bytes that a fresh
  `nix store optimise` would free, and the count of files that
  would be hard-linked.
* **Store breakdown** — inode counts by type (files / directories /
  symlinks).

With `--histograms`, power-of-two-bucketed size distributions are
appended:

* **NAR size distribution** across valid paths.
* **`.links/` size distribution** across unique contents, when the
  producer scanned `.links/` (always for local stores).

## JSON output (`--json`)

Top-level fields:

* `url` — the store identifier (same value as the Store URL line).
* `available` — `true` if stats are available, `false` otherwise.
  When `false`, no other fields are present.
* `pathCount` — number of valid store paths.
* `totalNarSize` — sum of each path's NAR size. **Logical**: a file
  shared across N paths contributes N times. Not a disk-usage figure;
  for that, see `fullWalk.totalDiskBytes`. Omitted from the human
  output because readers commonly mistake it for disk usage.
* `narSizeHistogram` — present only with `--histograms`; an array of
  `{bucket, low, high, count}` entries, bucketed by
  `floor(log2(narSize))`.

`dedup` (current hard-linking state, derived from `.links/`):

* `linksFileCount` — entries in `.links/`, i.e. unique file contents.
* `dedupedFileCount` — `.links/` entries with `nlink > 2` (a single
  unique content shared by 2+ store paths).
* `inodesSaved` — sum of `nlink - 2` over those entries: store-file
  inodes that hard-linking avoided creating.
* `uniqueBytes`, `uniqueDiskBytes` — sum of `st_size` and
  `st_blocks*512` for unique contents.
* `dedupBytes`, `dedupDiskBytes` — bytes that would be duplicated
  without hard-linking. Logical vs. on-disk.
* `sizeHistogram` — present only with `--histograms`; same
  `{bucket, low, high, count}` shape as `narSizeHistogram`,
  bucketed over `.links/` entry sizes.

Why `nlink > 2` for "deduplicated": a `.links/` entry has `nlink = 1`
(itself) plus one per store file referencing it; real sharing starts
at two referencing files.

`predictedDedup` (what a fresh `nix store optimise` would do now;
computed by hashing every unoptimised file in the walk):

* `filesLinkable` — additional files that would be replaced with a
  hard link.
* `bytesLinkable` — additional logical bytes that would be freed.

`fullWalk` (every reachable entry under the store directory, each
inode credited once):

* `totalDiskBytes` — closest equivalent to `du --block-size=1` on the
  store directory.
* `fileInodes`, `dirInodes`, `symlinkInodes` — inode counts by type.
* `totalInodes` — sum of the three counts above.

# Caveats

* `totalNarSize` is a logical sum that double-counts files shared
  across paths; treat it as an archive-size figure, not a disk-usage
  figure.
* On btrfs/ZFS with transparent compression, `st_blocks` reflects
  uncompressed extents rather than what reaches the device.
* The walk hashes every unoptimised file under a valid path, so the
  command's runtime scales with the unoptimised portion of the store.

Statistics are available for local stores and for daemon stores that
advertise the `query-store-stats` worker-protocol feature. Other
store types (binary caches, SSH stores) report that stats are
unavailable.

)""
