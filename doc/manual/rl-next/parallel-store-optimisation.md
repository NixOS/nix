---
synopsis: Parallel store optimisation
---

The command `nix-store --optimise` now supports a new configuration setting, `optimise-store-jobs`,
that allows users to set the maximum number of workers used to optimise the store.
The default remains 1.
