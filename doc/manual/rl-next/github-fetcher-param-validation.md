---
synopsis: GitHub fetcher now validates URL parameters
prs: [15331]
issues: [15304]
---

The `github:` fetcher now validates URL parameters, and will error if an invalid parameter like `tag` is provided.
