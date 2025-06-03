# Release 3.6.2 (2025-06-02)

* Based on [upstream Nix 2.29.0](../release-notes/rl-2.29.md).

## What's Changed
* Dramatically improve the performance of nix store copy-sigs: Use http-connections setting to control parallelism by @edolstra in [DeterminateSystems/nix-src#80](https://github.com/DeterminateSystems/nix-src/pull/80)
* Document how to replicate nix-store --query --deriver with the nix cli by @grahamc in [DeterminateSystems/nix-src#82](https://github.com/DeterminateSystems/nix-src/pull/82)
* The garbage collector no longer gives up if it encounters an undeletable file, by @edolstra in [DeterminateSystems/nix-src#83](https://github.com/DeterminateSystems/nix-src/pull/83)
* nix profile: Replace ε and ∅ with descriptive English words by @grahamc in [DeterminateSystems/nix-src#81](https://github.com/DeterminateSystems/nix-src/pull/81)
* Rework README to clarify that this distribution is our distribution, by @lucperkins in [DeterminateSystems/nix-src#84](https://github.com/DeterminateSystems/nix-src/pull/84)
* Include the source location when warning about inefficient double copies by @edolstra in [DeterminateSystems/nix-src#79](https://github.com/DeterminateSystems/nix-src/pull/79)
* Call out that `--keep-failed` with remote builders will keep the failed build directory on that builder by @cole-h in [DeterminateSystems/nix-src#85](https://github.com/DeterminateSystems/nix-src/pull/85)


**Full Changelog**: [v3.6.1...v3.6.2](https://github.com/DeterminateSystems/nix-src/compare/v3.6.1...v3.6.2)
