---
synopsis: "Introduce `libnixflake`"
prs: 9063
---

A new library, `libnixflake`, has been introduced to better separate the Flakes layer within Nix. This change refactors the codebase to encapsulate Flakes-specific functionality within its own library. 

See the commits in the pull request for detailed changes, with the only significant code modifications happening in the initial commit.

This change was alluded to in [RFC 134](https://github.com/nixos/rfcs/blob/master/rfcs/0134-nix-store-layer.md) and is a step towards a more modular and maintainable codebase.

Author: [**John Ericson (@Ericson2314)**](https://github.com/Ericson2314)
