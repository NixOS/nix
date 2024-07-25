---
synopsis: "nix3-build: show all FOD errors with `--keep-going`"
prs: 10734
---

The [`nix build`](@docroot@/command-ref/new-cli/nix3-build.md) command has been updated to improve the behavior of the [`--keep-going`] flag. Now, when `--keep-going` is used, all hash-mismatch errors of failing fixed-output derivations (FODs) are displayed, similar to the behavior of `nix build`. This enhancement ensures that all relevant build errors are shown, making it easier for users to update multiple derivations at once or to diagnose and fix issues.

Author: [**Jörg Thalheim (@Mic92)**](https://github.com/Mic92)

[`--keep-going`](@docroot@/command-ref/opt-common.md#opt-keep-going)
