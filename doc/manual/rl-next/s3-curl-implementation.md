---
synopsis: "Improved S3 binary cache support via HTTP"
prs: [13823, 14026, 14120, 14131, 14135, 14144, 14170, 14190, 14198, 14206, 14209, 14222, 14223, 13752]
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

All existing S3 URL formats and parameters remain supported, with the notable
exception of multi-part uploads, which are no longer supported.

Note that this change also means Nix now supports S3 binary cache stores even
if build without `aws-crt-cpp`, but only for public buckets which do not
require auth.
