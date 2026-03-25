---
synopsis: "`nix copy` to local stores downloads paths in parallel regardless of dependency order"
issues: [2559]
prs: []
---

When copying paths to a local store, `nix copy` previously waited for each
path's references to finish downloading and registering before starting the
download of that path. For closures with long dependency chains, this
serialized much of the transfer even when ample CPU and bandwidth were
available.

`LocalStore` now writes all paths to disk in parallel without waiting for
dependencies, then registers them as a batch in a single database transaction.
The closure invariant is still guaranteed: a path never becomes visible in the
store before its references do, because registration is atomic.

On partial failure with `--keep-going`, only the topologically-closed subset of
successfully written paths is registered; paths whose references failed are
left unregistered and reported.
