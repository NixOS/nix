---
synopsis: "Removed support for daemons and clients older than Nix 2.0"
prs: [13951]
---

We have dropped support in the daemon worker protocol for daemons and clients that don't speak at least version 18 of the protocol. This first Nix release that supports this version is Nix 2.0, released in February 2018.
