---
synopsis: Cache AWS credentials and refresh on S3 token expiry
prs: [15609]
---

Two improvements to AWS authentication for S3 binary caches:

- The credential provider chain is now cached. Previously every S3 request
  triggered a fresh STS / SSO / IMDS round-trip; credentials are now held for
  up to 15 minutes (or until their embedded expiration, if shorter).

- When an S3 request fails with an `ExpiredToken` or `TokenRefreshRequired`
  error code, Nix now asynchronously refreshes credentials and retries.
  Previously a session token expiring mid-build was a hard failure.
