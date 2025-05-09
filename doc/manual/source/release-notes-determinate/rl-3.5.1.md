# Release 3.5.1 (2025-05-09)

* Based on [upstream Nix 2.28.3](../release-notes/rl-2.28.md).

## What's Changed
* Deprecate upgrade-nix command in favor of `determinate-nixd upgrade`, by @gustavderdrache in [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)
* Lazy trees v2 by @edolstra in [DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27)
* Improve lazy trees backward compatibility by @edolstra in [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56)
* Canonicalize flake input URLs before checking flake.lock file staleness, for dealing with `dir` in URL-style flakerefs by @edolstra in [DeterminateSystems/nix-src#57](https://github.com/DeterminateSystems/nix-src/pull/57)
* Improve build failure error messages by @edolstra in [DeterminateSystems/nix-src#58](https://github.com/DeterminateSystems/nix-src/pull/58)

**Full Changelog**: [v3.4.2...v3.5.1](https://github.com/DeterminateSystems/nix-src/compare/v3.4.2...v3.5.1)
