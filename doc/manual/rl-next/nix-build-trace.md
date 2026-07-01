---
synopsis: Rename `nix realisation` to `nix build-trace`
prs: [15948]
---

The `nix realisation` subcommand, used for the unstable `ca-derivations` feature,
has been renamed to `nix store build-trace`.
The term "realisation" and "realise" are used for a number of different purposes,
and the rename aims to be more clear.
