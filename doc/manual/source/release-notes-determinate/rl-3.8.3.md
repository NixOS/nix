# Release 3.8.3 (2025-07-18)

* Based on [upstream Nix 2.30.1](../release-notes/rl-2.30.md).

## What's Changed

### Non-blocking evaluation caching

Users reported evaluation would occasionally block other evaluation processes.

The evaluation cache database is now opened in write-ahead mode to prevent delaying evaluations.

PR: [DeterminateSystems/nix-src#150](https://github.com/DeterminateSystems/nix-src/pull/150)

### New experimental feature: `external-builders`

This experimental feature allows Nix to call an external program for the build environment.

The interface and behavior of this feature may change at any moment without a correspondingly major semver version change.

PRs:
- [DeterminateSystems/nix-src#141](https://github.com/DeterminateSystems/nix-src/pull/141)
- [DeterminateSystems/nix-src#152](https://github.com/DeterminateSystems/nix-src/pull/152)
- [DeterminateSystems/nix-src#78](https://github.com/DeterminateSystems/nix-src/pull/78)

**Full Changelog**: [v3.8.2...v3.8.3](https://github.com/DeterminateSystems/nix-src/compare/v3.8.2...v3.8.3)
