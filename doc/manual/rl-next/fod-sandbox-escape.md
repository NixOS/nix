---
synopsis: Fix a FOD sandbox escape
issues:
prs:
---

Cooperating Nix derivations could send file descriptors to files in the Nix
store to each other via Unix domain sockets in the abstract namespace. This
allowed one derivation to modify the output of the other derivation, after Nix
has registered the path as "valid" and immutable in the Nix database.
In particular, this allowed the output of fixed-output derivations to be
modified from their expected content.

This isn't the case any more.
