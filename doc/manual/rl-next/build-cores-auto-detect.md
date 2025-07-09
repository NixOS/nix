---
synopsis: "`build-cores = 0` now auto-detects CPU cores"
prs: [13402]
---

When `build-cores` is set to `0`, nix now automatically detects the number of available CPU cores and passes this value via `NIX_BUILD_CORES`, instead of passing `0` directly. This matches the behavior when `build-cores` is unset. This prevents the builder from having to detect the number of cores.
