# Release 3.5.1 (2025-05-09)

* Based on [upstream Nix 2.28.3](../release-notes/rl-2.28.md).

## What's Changed
* Only run the test step after build completes by @grahamc in [DeterminateSystems/nix-src#51](https://github.com/DeterminateSystems/nix-src/pull/51)
* Remove flake-compat input by @lucperkins in [DeterminateSystems/nix-src#52](https://github.com/DeterminateSystems/nix-src/pull/52)
* Deprecate upgrade-nix command by @gustavderdrache in [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)
* Update flake.lock in light of recent change by @lucperkins in [DeterminateSystems/nix-src#54](https://github.com/DeterminateSystems/nix-src/pull/54)
* Lazy trees v2 by @edolstra in [DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27)
* Improve lazy trees backward compatibility by @edolstra in [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56)
* Canonicalize flake input URLs before checking flake.lock file staleness, for dealing with `dir` in URL-style flakerefs by @edolstra in [DeterminateSystems/nix-src#57](https://github.com/DeterminateSystems/nix-src/pull/57)
* Improve build failure error messages by @edolstra in [DeterminateSystems/nix-src#58](https://github.com/DeterminateSystems/nix-src/pull/58)
* Release v3.5.0 by @github-actions in [DeterminateSystems/nix-src#59](https://github.com/DeterminateSystems/nix-src/pull/59)

## New Contributors
* @gustavderdrache made their first contribution in [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)

**Full Changelog**: [v3.4.2...v3.5.1](https://github.com/DeterminateSystems/nix-src/compare/v3.4.2...v3.5.1)
