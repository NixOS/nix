# Release 3.5.2 (2025-05-12)

* Based on [upstream Nix 2.28.3](../release-notes/rl-2.28.md).

## What's Changed
* Fix a regression where narHash was not added to lock files when lazy trees were disabled by @edolstra in [DeterminateSystems/nix-src#63](https://github.com/DeterminateSystems/nix-src/pull/63)

* Tell users a source is corrupted ("cannot read file from tarball: Truncated tar archive detected while reading data"), improving over the previous 'cannot read file from tarball' error by @edolstra in [DeterminateSystems/nix-src#64](https://github.com/DeterminateSystems/nix-src/pull/64)


**Full Changelog**: [v3.5.1...v3.5.2](https://github.com/DeterminateSystems/nix-src/compare/v3.5.1...v3.5.2)
