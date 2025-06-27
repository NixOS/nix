# Release 3.6.7 (2025-06-24)

* Based on [upstream Nix 2.29.1](../release-notes/rl-2.29.md).

## What's Changed

### Security contents

* Patched against GHSA-g948-229j-48j3

### Lazy trees:

* Lazy trees now produces `flake.lock` files with NAR hashes unless `lazy-locks` is set to `true` by @edolstra in [DeterminateSystems/nix-src#113](https://github.com/DeterminateSystems/nix-src/pull/113)
* Improved caching with lazy-trees when using --impure, with enhanced testing by @edolstra in [DeterminateSystems/nix-src#117](https://github.com/DeterminateSystems/nix-src/pull/117)


**Full Changelog**: [v3.6.6...v3.6.7](https://github.com/DeterminateSystems/nix-src/compare/v3.6.6...v3.6.7)
