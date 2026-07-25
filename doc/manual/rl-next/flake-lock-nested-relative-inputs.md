---
synopsis: "Fix locking of relative `path:` inputs in deeply nested flakes"
issues: [14762]
---

Locking a flake that transitively depends on a flake with a relative `path:`
input (three or more levels deep) no longer fails with
`path '.../flake.nix' does not exist`. The relative input is now resolved
against the correct flake's source tree.
