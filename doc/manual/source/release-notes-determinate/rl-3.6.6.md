# Release 3.6.6 (2025-06-17)

* Based on [upstream Nix 2.29.0](../release-notes/rl-2.29.md).

## What's Changed
* Release v3.6.4 by @github-actions in [DeterminateSystems/nix-src#109](https://github.com/DeterminateSystems/nix-src/pull/109)
* Fixup the complainy docs line by @grahamc in [DeterminateSystems/nix-src#111](https://github.com/DeterminateSystems/nix-src/pull/111)
* Move the actual vm tests / flake regressions into the generic build phase by @grahamc in [DeterminateSystems/nix-src#112](https://github.com/DeterminateSystems/nix-src/pull/112)
* Fix broken fetchToStore() caching by @edolstra in [DeterminateSystems/nix-src#110](https://github.com/DeterminateSystems/nix-src/pull/110)
* Parallelize the flake regression suite by @grahamc in [DeterminateSystems/nix-src#114](https://github.com/DeterminateSystems/nix-src/pull/114)
* Don't build fallback-paths if we didn't build aarch64-linux and x86 d…   …arwin by @grahamc in [DeterminateSystems/nix-src#116](https://github.com/DeterminateSystems/nix-src/pull/116)
* Use GHA runners for VMs since they have KVM by @grahamc in [DeterminateSystems/nix-src#118](https://github.com/DeterminateSystems/nix-src/pull/118)
* fetchToStore() cache: Use content hashes instead of store paths by @edolstra in [DeterminateSystems/nix-src#115](https://github.com/DeterminateSystems/nix-src/pull/115)
* Release v3.6.5 by @github-actions in [DeterminateSystems/nix-src#119](https://github.com/DeterminateSystems/nix-src/pull/119)


**Full Changelog**: [v3.6.3...v3.6.6](https://github.com/DeterminateSystems/nix-src/compare/v3.6.3...v3.6.6)
