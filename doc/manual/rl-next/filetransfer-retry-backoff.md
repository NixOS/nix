---
synopsis: Configurable file-transfer retry backoff with full jitter and Retry-After support
issues: [15016, 15419, 15023]
---

File transfer retries (downloads and uploads) now use AWS-style "full jitter"
exponential backoff, treat HTTP 503 as rate-limited (same longer delay as 429),
and honor the `Retry-After` response header. Retry timing is configurable via
new `nix.conf` settings:

- `filetransfer-retry-delay` (default 100ms): base delay for transient errors
- `filetransfer-retry-delay-rate-limited` (default 5000ms): base delay for 429/503
- `filetransfer-retry-max-delay` (default 60000ms): per-attempt delay ceiling
- `filetransfer-retry-jitter` (default true): enable full jitter

The existing `download-attempts` setting has been renamed to
`filetransfer-retry-attempts` to reflect that it applies to uploads as well as
downloads. The old name remains as an alias for backwards compatibility.

Per-substituter overrides are available as store URL parameters
(`retry-delay`, `retry-delay-rate-limited`, `retry-max-delay`,
`retry-attempts`), e.g. `s3://my-cache?retry-attempts=8`.

This addresses thundering-herd scenarios where many CI jobs hit the same
S3 prefix and receive 503 SlowDown; previously the retry window for 503
was only ~4 seconds.
