# Release 3.8.0 (2025-07-10)

* Based on [upstream Nix 2.30.0](../release-notes/rl-2.30.md).

## What's Changed

### Faster CI with `nix flake check`

`nix flake check` no longer downloads flake outputs if no building is necessary.

This command is intended to validate that a flake can fully evaluate and all outputs can build.
If the outputs are available in a binary cache then both properties are confirmed to be true.
Notably, downloading the output from the binary cache is not strictly necessary for the validation.

Previously, `nix flake check` would download a flake output if the full build is available in a binary cache.

Some users will find this change significantly reduces costly bandwidth and CI workflow time.

PR: [DeterminateSystems/nix-src#134](https://github.com/DeterminateSystems/nix-src/pull/134)

### Improved flake locking of transitive dependencies

Determinate Nix now re-locks all transitive dependencies when changing a flake input's source URL.

This fixes an issue where in some scenarios Nix would not re-lock those inputs and incorrectly use the old inputs' dependencies.

PR: [DeterminateSystems/nix-src#137](https://github.com/DeterminateSystems/nix-src/pull/137)

**Full Changelog**: [v3.7.0...v3.8.0](https://github.com/DeterminateSystems/nix-src/compare/v3.7.0...v3.8.0)
