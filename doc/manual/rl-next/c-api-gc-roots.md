---
synopsis: "C API: `nix_store_add_temp_root` and `nix_store_add_perm_root`"
prs: [16674]
---

The new functions `nix_store_add_temp_root()` and `nix_store_add_perm_root()` in `nix_api_store.h` register a
temporary and a permanent garbage collector root for a store path.
A permanent root is a symlink at the given location and is only available on local filesystem stores.
