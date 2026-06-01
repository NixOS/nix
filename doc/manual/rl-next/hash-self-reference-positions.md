---
synopsis: "Fix hash collision between store paths with self-references and their zeroed-out equivalents"
issues: [15837]
prs: [15931]
---

When computing the hash of a NAR with self-references, Nix zeroes out the self-references but also hashes their positions.
The latter was accidentally lost in Nix 2.17.0, which meant a NAR with self-references could hash to the same store path as an otherwise-identical NAR in which some of the self-references had been zeroed out.

This release restores hashing the positions of self-references.
As a consequence, content-addressed store paths derived from self-referential NARs will differ from those produced by Nix 2.17 through 2.34.
This affects users of the experimental `ca-derivations` features, as well as users of `nix store make-content-addressed`.
