---
synopsis: Remove experimental repl-flake
significance: significant
issues: 10103
prs: 10299
---

The `repl-flake` experimental feature has been removed. The `nix repl` command now works like the rest of the new CLI in that `nix repl {path}` now tries to load a flake at `{path}` (or fails if the `flakes` experimental feature isn't enabled).*
