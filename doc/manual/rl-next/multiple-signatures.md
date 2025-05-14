---
synopsis: "Multiple signatures support in store urls"
issues:
prs: [12976]
---

Added support for a `secretKeyFiles` URI parameter in Nix store URIs, allowing multiple signing key files to be specified as a comma-separated list.
This enables signing paths with multiple keys. This helps with [RFC #149](https://github.com/NixOS/rfcs/pull/149) to enable binary cache key rotation in the NixOS infra.

Example usage:

```bash
nix copy --to "file:///tmp/store?secret-keys=/tmp/key1,/tmp/key2" \
  "$(nix build --print-out-paths nixpkgs#hello)"
```
