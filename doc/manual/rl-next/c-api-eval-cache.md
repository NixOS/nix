---
synopsis: "C API: walk the evaluation cache of a locked flake"
prs: [16676]
---

The new header `nix_api_eval_cache.h` exposes the evaluation cache of a locked flake.
`nix_eval_cache_open()` opens the cache that the `nix` command uses for the flake, and a `nix_attr_cursor` walks its
attributes: `nix_attr_cursor_maybe_get_attr()`, `nix_attr_cursor_get_attrs()`, `nix_attr_cursor_is_derivation()`,
`nix_attr_cursor_get_drv_path()`, `nix_attr_cursor_get_string()`, `nix_attr_cursor_get_bool()`,
`nix_attr_cursor_get_list_of_strings()` and `nix_attr_cursor_force_value()`.
Attributes already in the cache are read without evaluation, the rest are evaluated and cached.
`nix_eval_cache_checkpoint()` folds the write-ahead log of the cache into its database file.
