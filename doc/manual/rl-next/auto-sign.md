---
synopsis: Automatically generate and use a signing key
prs: [15708]
issues: [3023]
---

Sets the default value of `secret-key-files` to /nix/var/nix/keys/secret-key, and automatically generates this keypair if it doesn't exist. The effect of this is that locally built paths are always signed, making it easier to trace where builds come from and to establish trust. The corresponding public key is stored at `/nix/var/nix/public-keys/public-key`.
