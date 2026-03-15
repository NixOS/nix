---
synopsis: "`nix copy` queries source path info in parallel"
issues: [5118]
prs: []
---

When copying from an HTTP binary cache, `nix copy` previously queried
`.narinfo` files for all missing paths serially before starting any
data transfer. These queries now fan out concurrently via the async
path-info API, removing up to N round-trips of latency when copying
N paths. The effective concurrency is governed by the
`http-connections` setting and HTTP/2 multiplexing.
