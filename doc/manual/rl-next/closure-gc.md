---
synopsis: "Added `nix store gc-closure` for collecting garbage within a closure"
issues: 7239
prs: 15236
---

`nix store gc-closure` can be called with installable arguments, in which case it
will only collect the dead paths that are part of the closure of its arguments.

Also added a utility function `nix dev-env-path` to get the store path of the
actual build environment that would be used by commands like `nix develop`.
