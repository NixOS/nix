This section describes the notion of *experimental features*, and how it fits into the big picture of the development of Nix.

# What are experimental features?

[Since Nix 2.4](../release-notes/rl-2.4.md), some features of Nix can be flagged as experimental.
Experimental features can be changed or removed at any time, and users must [explicitly enable them].
This allows accessing new functionality that is not yet stable without unwittingly relying on them.

[explicitly enable them]: ../nix-conf.md#conf-expeirmental-features

# When should a new feature be marked experimental?

A change in the Nix codebase should be guarded by an experimental feature flag if it is likely to either introduce a regression of any kind, or if it implies a non-trivial change to the external interface of Nix.

Examples:

- Changes to the Nix language, such as new built-ins, syntactic or semantic changes, etc.
- Changes to the command-line interface

# Lifecycle of an experimental feature

Experimental features have to be treated on a case-by-case basis.
However, the standard workflow for an experimental feature is as follows:

- The feature is implemented in a pull request.
- It is merged under an experimental flag, and is disabled by default.
    - Being experimental, the feature can still be changed arbitrarily or removed again.
- After at least one release cycle without any significant issues, the feature becomes enabled by default.
    - If any blocking issue gets reported the feature can be disabled by default again.
- After at least another release cycle, the feature flag is removed and the feature is considered stable.

The following diagram illustrates the process:

<!-- TODO: replace with ASCII art to render correctly once contents are agreed upon -->

```mermaid
flowchart
    xp_disabled[experimental, disabled by default]
    xp_enabled[experimental, enabled by default]
    stable([stable])
    trunk([pull request])
    dropped([removed])


    trunk --> |merge into master| xp_disabled
    xp_disabled --> |At least one release without significant issues| xp_enabled
    xp_enabled --> |At least one release without significant issues| stable
    xp_enabled --> |Blocking issue reported| xp_disabled
    xp_disabled --> |"(breaking) changes"| xp_disabled
    xp_disabled --> |Decision against adding the feature| dropped
```

This is just indicative.
For instance, some features have to be used deliberately anyway, such as an additional command-line flag: there's no point in making them experimental but enabled by default.

# Relation to the RFC process

Experimental features and [RFCs](https://github.com/NixOS/rfcs/) both allow approaching substantial changes while minimizing the risk.
However they serve different purposes:

- An experimental feature enables developers to iterate on and deliver a new idea without committing to it or requiring a costly long-running fork.
  It is primarily an issue of *implementation*, targeting Nix developers and early testers.
- The goal of an RFC is to make explicit all the implications of a change: Explain why it is wanted, which new use-cases it enables, which interface changes it requires, etc.
  It is primarily an issue of *design* and *communication*, targeting the broader community.


This means that experimental features and RFCs are orthogonal mechanisms, and can be used independently or together as needed.
