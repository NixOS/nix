- The `compress-build-log` setting now accepts a compression algorithm name
  (e.g. `zstd`, `gzip`, `xz`, `lz4`, `br`, `none`) instead of only `true`/`false`.
  The default remains `bzip2` for backward compatibility.
  `true` and `false` are still accepted as aliases for `bzip2` and `none`.

  Reading build logs now supports all compression formats regardless of the
  current setting, so logs remain readable after switching algorithms.
