---
synopsis: "Improved S3 binary cache support via HTTP"
prs: [13752, 13823, 14026, 14120, 14131, 14135, 14144, 14170, 14190, 14198, 14206, 14209, 14222, 14223, 14330, 14333, 14335, 14336, 14337, 14350, 14356, 14357, 14374, 14375, 14376, 14377, 14391, 14393, 14420, 14421]
issues: [13084, 12671, 11748, 12403]
---

S3 binary cache operations now happen via HTTP, leveraging `libcurl`'s native
AWS SigV4 authentication instead of the AWS C++ SDK, providing significant
improvements:

- **Reduced memory usage**: Eliminates memory buffering issues that caused
  segfaults with large files
- **Fixed upload reliability**: Resolves AWS SDK chunking errors
  (`InvalidChunkSizeError`)
- **Lighter dependencies**: Uses lightweight `aws-crt-cpp` instead of full
  `aws-cpp-sdk`, reducing build complexity

The new implementation requires curl >= 7.75.0 and `aws-crt-cpp` for credential
management.

All existing S3 URL formats and parameters remain supported, however the store
settings for configuring multipart uploads have changed:

- **`multipart-upload`** (default: `false`): Enable multipart uploads for large
  files. When enabled, files exceeding the multipart threshold will be uploaded
  in multiple parts.

- **`multipart-threshold`** (default: `100 MiB`): Minimum file size for using
  multipart uploads. Files smaller than this will use regular PUT requests.
  Only takes effect when `multipart-upload` is enabled.

- **`multipart-chunk-size`** (default: `5 MiB`): Size of each part in multipart
  uploads. Must be at least 5 MiB (AWS S3 requirement). Larger chunk sizes
  reduce the number of requests but use more memory.

- **`buffer-size`**: Has been replaced by `multipart-chunk-size` and is now an alias to it.

Note that this change also means Nix now supports S3 binary cache stores even
if built without `aws-crt-cpp`, but only for public buckets which do not
require authentication.
