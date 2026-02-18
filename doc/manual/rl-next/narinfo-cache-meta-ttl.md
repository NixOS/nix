---
synopsis: New setting `narinfo-cache-meta-ttl`
prs: [15287]
---

The new setting `narinfo-cache-meta-ttl` controls how long binary cache metadata (i.e. `/nix-cache-info`) is cached locally, in seconds. This was previously hard-coded to 7 days, which is still the default. As a result, you can now use `nix store info --refresh` to check whether a binary cache is still valid.
