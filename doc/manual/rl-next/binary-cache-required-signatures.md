---
synopsis: "Binary caches can now soft-enforce signature requirements"
issues: [12491]
---

Binary caches can now advertise signature requirements through their
`nix-cache-info` file, preventing accidental uploads of unsigned or
incorrectly-signed store paths.

Cache operators can add a `RequiredSignatures` field containing a
whitespace-separated list of public keys. When uploading paths to such caches,
Nix validates that each path has at least one valid signature from the required
keys:

```
StoreDir: /nix/store
RequiredSignatures: cache.example.org-1:abc123... cache.example.org-2:def456...
```

This helps catch common configuration errors, such as typos in store URLs
(`secret-key-file` vs `secret-key`), by failing fast with clear error messages
rather than silently uploading unsigned paths.

For multi-party approval workflows, the optional `RequireAllSignatures: 1` field
requires paths to be signed by *all* listed keys rather than just one:

```
StoreDir: /nix/store
RequiredSignatures: dev:key1... qa:key2... release:key3...
RequireAllSignatures: 1
```

Content-addressed paths are automatically exempt from signature requirements as
they are self-validating.
