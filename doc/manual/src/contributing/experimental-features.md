This section describes the notion of *experimental features*, and how it fits into the big picture of the development of Nix.

# What are experimental features?

Experimental features are considered unstable, which means that they can be changed or removed at any time.
Users must explicitly enable them by toggling the associated [experimental feature flags](@docroot@/command-ref/conf-file.md#conf-experimental-features).
This allows accessing unstable functionality without unwittingly relying on it.

Experimental feature flags were first introduced in [Nix 2.4](@docroot@/release-notes/rl-2.4.md).
Before that, Nix did have experimental features, but they were not guarded by flags and were merely documented as unstable.
This was a source of confusion and controversy.

# When should a new feature be marked experimental?

A change in the Nix codebase should be guarded by an experimental feature flag if it is considered likely to be reverted or adapted in a backwards-incompatible manner after gathering more experience with it in practice.

Examples:

- Changes to the Nix language, such as new built-ins, syntactic or semantic changes, etc.
- Changes to the command-line interface

# Lifecycle of an experimental feature

Experimental features have to be treated on a case-by-case basis.
However, the standard workflow for an experimental feature is as follows:

- A new feature is implemented in a *pull request*
  - It is guarded by an experimental feature flag that is disabled by default
- The pull request is merged, the *experimental* feature ends up in a release
    - Using the feature requires explicitly enabling it, signifying awareness of the potential risks
    - Being experimental, the feature can still be changed arbitrarily
- The feature can be *removed*
  - The associated experimental feature flag is also removed
- The feature can be declared *stable*
  - The associated experimental feature flag is removed
  - There should be enough evidence of users having tried the feature, such as feedback, fixed bugs, demonstrations of how it is put to use
  - Maintainers must feel confident that:
    - The feature is designed and implemented sensibly, that it is fit for purpose
    - Potential interactions are well-understood
    - Stabilising the feature will not incur an outsized maintenance burden in the future

The following diagram illustrates the process:

```
                  .------.
                  | idea |
                  '------'
                      |
       discussion, design, implementation
                      |
                      |     .-------.
                      |     |       |
                      v     v       |
               .--------------.  review
               | pull request |     |
               '--------------'     |
                   |     ^  |       |
                   |     |  '-------'
               .---'     '----.
               |              |
             merge       user feedback,
               |       (breaking) changes
               |              |
               '---.     .----'
                   |     |
                   v     |
               +--------------+
           .---| experimental |----.
           |   +--------------+    |
           |                       |
decision to stabilise      decision against
           |              keeping the feature
           |                       |
           v                       v
       +--------+             +---------+
       | stable |             | removed |
       +--------+             +---------+
```

# Relation to the RFC process

Experimental features and [RFCs](https://github.com/NixOS/rfcs/) both allow approaching substantial changes while minimizing the risk.
However they serve different purposes:

- An experimental feature enables developers to iterate on and deliver a new idea without committing to it or requiring a costly long-running fork.
  It is primarily an issue of *implementation*, targeting Nix developers and early testers.
- The goal of an RFC is to make explicit all the implications of a change:
  Explain why it is wanted, which new use-cases it enables, which interface changes it requires, etc.
  It is primarily an issue of *design* and *communication*, targeting the broader community.

This means that experimental features and RFCs are orthogonal mechanisms, and can be used independently or together as needed.

# Currently available experimental features

{{#include ./experimental-feature-descriptions.md}}
