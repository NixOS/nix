# Release 3.8.5 (2025-08-04)

* Based on [upstream Nix 2.30.2](../release-notes/rl-2.30.md).

## What's Changed
* Apply upstream formatting changes by @edolstra in [DeterminateSystems/nix-src#159](https://github.com/DeterminateSystems/nix-src/pull/159)
* Avoid isValidPath(), use queryPathInfo() instead by @edolstra in [DeterminateSystems/nix-src#157](https://github.com/DeterminateSystems/nix-src/pull/157)
* Imply --offline during tab completion by @edolstra in [DeterminateSystems/nix-src#161](https://github.com/DeterminateSystems/nix-src/pull/161)
* SQLite: fsync db.sqlite-shm before opening the database by @edolstra in [DeterminateSystems/nix-src#158](https://github.com/DeterminateSystems/nix-src/pull/158)
* Make GitFileSystemObjectSink multi-threaded by @edolstra in [DeterminateSystems/nix-src#149](https://github.com/DeterminateSystems/nix-src/pull/149)
* Sync with upstream 2.30.2 by @edolstra in [DeterminateSystems/nix-src#160](https://github.com/DeterminateSystems/nix-src/pull/160)
* Add paths to the store asynchronously by @edolstra in [DeterminateSystems/nix-src#162](https://github.com/DeterminateSystems/nix-src/pull/162)
* Remove obsolete FIXME by @edolstra in [DeterminateSystems/nix-src#163](https://github.com/DeterminateSystems/nix-src/pull/163)
* Rename detsys-main to main by @grahamc in [DeterminateSystems/nix-src#164](https://github.com/DeterminateSystems/nix-src/pull/164)
* fixup: perpetual PR number by @cole-h in [DeterminateSystems/nix-src#166](https://github.com/DeterminateSystems/nix-src/pull/166)


**Full Changelog**: [v3.8.4...v3.8.5](https://github.com/DeterminateSystems/nix-src/compare/v3.8.4...v3.8.5)
