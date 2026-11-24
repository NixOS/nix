---
synopsis: New flag --preserve-cwd to nix develop
prs: [15337]
issues: [7361]
---

This addresses issues where `nix develop --phase` is running against a flake in the Nix store and/or a flake registry. By default, the working directory will be changed to the flake root before running the requested phase, which can break `--unpack` when it tries to write to a read-only filesystem. `--preserve-cwd` will run the phase without changing directory to the flake root.
