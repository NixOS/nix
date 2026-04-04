---
synopsis: zstd compression now emits multi-frame output for future parallel decoding
prs: [15550]
---

zstd-compressed NARs are now written as a concatenation of independent frames
(one per 16 MiB of uncompressed input) rather than a single frame. Each frame
header carries an exact `Frame_Content_Size`, so a future parallel decoder can
split work across cores without any change to data already on disk.

Frame concatenation is required by RFC 8878 §3.1, so existing Nix binaries,
libarchive, and the `zstd` CLI all decode the new output unchanged.

Each frame is compressed using up to 4 worker threads. This is now the default
for zstd: the `parallel-compression` store setting defaults to `true` when
`compression=zstd` (it remains `false` for `xz`). Set
`?parallel-compression=false` to opt out. Previously, libarchive's zstd filter
produced a single frame regardless of thread count, so decompression of large
NARs was bottlenecked on one core.
