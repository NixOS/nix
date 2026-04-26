---
synopsis: "Added `--skip-alive` option to `nix store delete` for collecting garbage within a closure"
issues: 7239
prs: [15236, 15727]
---

`nix store delete --recursive --skip-alive` can be used to collect garbage
within a closure, in which case it will only collect the dead paths that are
part of the closure of its arguments. The additional option
`--delete-dead-referrers` is also added to support this mode, which allows
dead referrers of paths in the closure to also be deleted.
