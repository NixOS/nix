# Condense information around Nix 3.0

- Status: informational
- Date: 2022-03-11

## Context

We have a lot of feedback and arguments,
from different people,
in different sources (comments, issues, discourse),
around a Nix 3.0 release,
principally around Flakes.

Being able to draw a short,
consistent mental model of such feedback
would allows us to have a clear understanding of the why.

Once we understand the why of everything,
it's easy to shape an implementation.

# Criticism of current design (Nix 2.x, default configuration)

> "Reproducible builds and deployments" - https://nixos.org

Nix builds and deployments can be reproducible,
provided you avoid common temptations - see below.

However, Nix expressions are not reproducible,
which in practice extends to either:

- Making the build not reproducible.
- Making it unnecessarily hard to reproduce the build.

This problem originates from the following mechanisms:

- Env vars access (builtins.getEnv).
- Current time access (builtins.currentTime).
- Fetchers infrastructure works without a hash (builtins.fetch\*, etc).
- Arbitrary file system access
  outside of the concept of a project
  (e.g. `~/.config/nixpkgs/config.nix`).
- `NIX_PATH`.

  - Depends on the environment.
  - Cannot be pinned easily.
  - Differs across machines.
  - Is not easily portable.
  - Open the door for ad-hoc hacks.
  - Is imperative instead of declarative.
  - Cannot be shipped as part of the expression.

# Goals

## Making Nix expressions reproducible

### Enable purity and reproducibility by default

- No access to current time (hermeticity).
- Fetchers require a hash (reproducibility).
- No access to env vars (hermeticity).
- Add a `--impure` flag as a escape hatch
  (same as you can disable sandbox if you wish).
  This eases migration.

### Remove NIX_PATH and make sharing of code a thing

- Define a concept of project (hermeticity is relative)
  - No access to arbitrary places in the FS outside of it.
- Turn release.nix into a real, recognized, documented interface and schema (discoverability).
- Define an official interface for importing code from other people as input (composability & extensibility).

# Pending to do

- Analyze the 551 comments of [RFC0049](https://github.com/NixOS/rfcs/pull/49).

- Explain how Flakes fit into the model and solve this problem once and for all.

- Have some stats about what the more controversial points are
  since this can bring some light into what is really important,
  niche, worked around, or postponed, etc.
