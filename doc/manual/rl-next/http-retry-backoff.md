---
synopsis: Configurable HTTP retry backoff with full jitter and Retry-After support
issues: [15016, 15419, 15023]
---

HTTP transfer retries (downloads and uploads) now use AWS-style "full jitter"
exponential backoff, treat HTTP 503 as rate-limited (same longer delay as 429),
and honor the `Retry-After` response header. Retry timing is configurable via
new `nix.conf` settings:

- `http-retry-delay` (default 250ms): base delay for transient errors
- `http-retry-delay-rate-limited` (default 5000ms): base delay for 429/503
- `http-retry-max-delay` (default 60000ms): per-attempt delay cap
- `http-retry-jitter` (default true): enable full jitter

Per-substituter overrides are available as store URL parameters
(`retry-delay`, `retry-delay-rate-limited`, `retry-max-delay`,
`retry-attempts`), e.g. `s3://my-cache?retry-attempts=8`.

This addresses thundering-herd scenarios where many CI jobs hit the same
S3 prefix and receive 503 SlowDown; previously the retry window for 503
was only ~4 seconds.
