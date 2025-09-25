---
synopsis: "Substituted flake inputs are no longer re-copied to the store"
prs: [14041]
---

Since 2.25, Nix would fail to store a cache entry for substituted flake inputs,
which in turn would cause them to be re-copied to the store on initial
evaluation. Caching these inputs results in a near doubling of a performance in
some cases — especially on I/O-bound machines and when using commands that
fetch many inputs, like `nix flake archive/prefetch-inputs`
