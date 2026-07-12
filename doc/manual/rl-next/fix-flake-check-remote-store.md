---
synopsis: "`nix flake check --store` no longer silently skips checks the store cannot recognise"
prs: [16125]
---

Since 2.32, `nix flake check` skips derivations whose outputs are already
valid or substitutable in the target store. When the target store was not
the eval store (e.g. `--store ssh-ng://builder --eval-store auto`), the
freshly evaluated `.drv` files were never copied to the target store, so it
classified every check as "unknown" — and unknown derivations were silently
skipped. As a result, `nix flake check --store <store>` reported "all checks
passed" without building anything, including checks that would have failed.

Now, derivations whose outputs the store cannot determine are built rather
than skipped, and the derivation closures are copied to the target store
first (as `nix build` already did).
