---
synopsis: zstd compression now emits multi-frame output and uses less memory
prs: [15550]
---

zstd-compressed NARs are now written as a sequence of independent 16 MiB
frames instead of a single large frame. This lays the groundwork for
parallel decompression in a future release without requiring caches to be
repopulated, and significantly lowers peak memory use during compression
(e.g. from ~600 MiB to ~100 MiB for a 1 GiB store path).

The output remains standard zstd and is decoded unchanged by existing Nix
binaries and the `zstd` CLI; compression ratio is effectively unchanged.

Per-frame compression now uses up to 4 worker threads. For zstd this is the
new default: the `parallel-compression` store setting defaults to `true` when
`compression=zstd` (it remains `false` for `xz`). Set
`?parallel-compression=false` to opt out.
