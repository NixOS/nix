---
synopsis: "Temporary build directories no longer include derivation names"
prs: [13839]
---

Temporary build directories created during derivation builds no longer include the derivation name in their path to avoid build failures when the derivation name is too long. This change ensures predictable prefix lengths for build directories under `/nix/var/nix/builds`.