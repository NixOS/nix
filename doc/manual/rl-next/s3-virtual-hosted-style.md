---
synopsis: S3 binary caches now use virtual-hosted-style addressing by default
issues: [15208]
---

S3 binary caches now use virtual-hosted-style URLs
(`https://bucket.s3.region.amazonaws.com/key`) instead of path-style URLs
(`https://s3.region.amazonaws.com/bucket/key`) when connecting to standard AWS
S3 endpoints. This enables HTTP/2 multiplexing and fixes TCP connection
exhaustion (TIME_WAIT socket accumulation) under high-concurrency workloads.

A new `addressing-style` store option controls this behavior:

- `auto` (default): virtual-hosted-style for standard AWS endpoints, path-style
  for custom endpoints.
- `path`: forces path-style addressing (deprecated by AWS).
- `virtual`: forces virtual-hosted-style addressing (bucket names must not
  contain dots).

Bucket names containing dots (e.g., `my.bucket.name`) automatically fall back
to path-style addressing in `auto` mode, because dotted names create
multi-level subdomains that break TLS wildcard certificate validation.

Example using path-style for backwards compatibility:

```
s3://my-bucket/key?region=us-east-1&addressing-style=path
```

Additionally, TCP keep-alive is now enabled on all HTTP connections, preventing
idle connections from being silently dropped by intermediate network devices
(NATs, firewalls, load balancers).
