---
synopsis: Remounting a read-only store without a private mount namespace is now an error
---

When the Nix store is on a read-only mount, Nix tries to create a private
mount namespace via `unshare(CLONE_NEWNS)` before remounting the store writable.
Previously, if the `unshare` failed, the error was swallowed silently and Nix
still attempted the remount, which leaked onto the host mount table.

Nix now detects the missing private mount namespace up front and throws a
clear error. This typically occurs inside containers or user namespaces that
do not grant `CAP_SYS_ADMIN`.
