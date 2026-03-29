---
synopsis: "Added `--ignore-alive` option to `nix store delete` for collecting garbage within a closure"
issues: 7239
prs: 15236
---

`nix store delete --recursive --ignore-alive` can be used to collect garbage
within a closure, in which case it will only collect the dead paths that are
part of the closure of its arguments.
