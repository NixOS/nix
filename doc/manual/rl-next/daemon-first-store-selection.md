---
synopsis: Nix now prefers the daemon over direct store access
---

When using `auto` or the default store (without explicit `--store`), Nix now connects to the daemon if the socket exists, even when the user has write access to the store.

Previously, Nix would bypass the daemon and access the store directly if the store directory was writable. This led to several issues:
- Inconsistent behavior between users with different permissions
- The `max-jobs` counter not being shared between Nix instances

To explicitly use the local store without the daemon, use `--store local`.
