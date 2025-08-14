---
synopsis: "Improved S3 binary cache support via HTTP"
prs: [13752]
issues: [13084, 12671, 11748, 12403, 5947]
---

S3 binary cache operations now happen via HTTP, leveraging `libcurl`'s native AWS
SigV4 authentication instead of the AWS C++ SDK, providing significant
improvements:

- **Reduced memory usage**: Eliminates memory buffering issues that caused
  segfaults with large files (>3.5GB)
- **Fixed upload reliability**: Resolves AWS SDK chunking errors
  (`InvalidChunkSizeError`) during multipart uploads
- **Resolved OpenSSL conflicts**: No more S2N engine override issues in
  sandboxed builds
- **Lighter dependencies**: Uses lightweight `aws-crt-cpp` instead of full
  `aws-cpp-sdk`, reducing build complexity

The new implementation requires curl >= 7.75.0 and `aws-crt-cpp` for credential
management.

All existing S3 URL formats and parameters remain supported.

## Breaking changes

The legacy `S3BinaryCacheStore` implementation has been removed in favor of the
new curl-based approach.

**Migration**: No action required for most users. S3 URLs continue to work
with the same syntax. Users directly using `S3BinaryCacheStore` class
should migrate to standard HTTP binary cache stores with S3 endpoints.

**Build requirement**: S3 support now requires curl >= 7.75.0 for AWS SigV4
authentication. Build configuration will warn if `aws-crt-cpp` is available
but S3 support is disabled due to an insufficient curl version.
