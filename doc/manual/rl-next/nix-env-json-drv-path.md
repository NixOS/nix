---
synopsis: Fix `nix-env --query --drv-path --json`
prs: 9257
---

Fixed a bug where `nix-env --query` ignored `--drv-path` when `--json` was set.
