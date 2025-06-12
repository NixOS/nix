# Release 3.6.4 (2025-06-12)

* Based on [upstream Nix 2.29.0](../release-notes/rl-2.29.md).

## What's Changed
* When remote building with --keep-failed, only show "you can rerun" message if the derivation's platform is supported on this machine by @cole-h in [DeterminateSystems/nix-src#87](https://github.com/DeterminateSystems/nix-src/pull/87)
* Indicate that sandbox-paths specifies a missing file in the corresponding error message. by @cole-h in [DeterminateSystems/nix-src#88](https://github.com/DeterminateSystems/nix-src/pull/88)
* Use 'published' release type to avoid double uploads by @gustavderdrache in [DeterminateSystems/nix-src#90](https://github.com/DeterminateSystems/nix-src/pull/90)
* Render lazy tree paths in messages withouth the/nix/store/hash... prefix in substituted source trees by @edolstra in [DeterminateSystems/nix-src#91](https://github.com/DeterminateSystems/nix-src/pull/91)
* Use FlakeHub inputs by @lucperkins in [DeterminateSystems/nix-src#89](https://github.com/DeterminateSystems/nix-src/pull/89)
* Proactively cache more flake inputs and fetches by @edolstra in [DeterminateSystems/nix-src#93](https://github.com/DeterminateSystems/nix-src/pull/93)
* Fix: register extra builtins just once by @edolstra in [DeterminateSystems/nix-src#97](https://github.com/DeterminateSystems/nix-src/pull/97)
* Fix: Make the S3 test more robust by @gustavderdrache in [DeterminateSystems/nix-src#101](https://github.com/DeterminateSystems/nix-src/pull/101)
* Fix the link to `builders-use-substitutes` documentation for `builders` by @lucperkins in [DeterminateSystems/nix-src#102](https://github.com/DeterminateSystems/nix-src/pull/102)
* Improve error messages that use the hypothetical future tense of "will" by @lucperkins in [DeterminateSystems/nix-src#92](https://github.com/DeterminateSystems/nix-src/pull/92)
* Make the `nix repl` test more stable by @edolstra in [DeterminateSystems/nix-src#103](https://github.com/DeterminateSystems/nix-src/pull/103)
* Run nixpkgsLibTests against lazy trees by @edolstra in [DeterminateSystems/nix-src#100](https://github.com/DeterminateSystems/nix-src/pull/100)
* Go back to x86 native macOS builds by @grahamc in [DeterminateSystems/nix-src#104](https://github.com/DeterminateSystems/nix-src/pull/104)
* Improve caching of inputs in dry-run mode by @edolstra in [DeterminateSystems/nix-src#98](https://github.com/DeterminateSystems/nix-src/pull/98)
* Run the Nix test suite with lazy trees enabled by @edolstra in [DeterminateSystems/nix-src#105](https://github.com/DeterminateSystems/nix-src/pull/105)
* Re-supply inadvertently deleted word by @lucperkins in [DeterminateSystems/nix-src#107](https://github.com/DeterminateSystems/nix-src/pull/107)
* Release v3.6.3 by @github-actions in [DeterminateSystems/nix-src#106](https://github.com/DeterminateSystems/nix-src/pull/106)


**Full Changelog**: [v3.6.2...v3.6.4](https://github.com/DeterminateSystems/nix-src/compare/v3.6.2...v3.6.4)
