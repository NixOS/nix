---
synopsis: "C API: functions for locking and loading a flake"
issues: 10435
prs: [12877, 13098]
---

This release adds functions to the C API for handling the loading of flakes. Previously, this had to be worked around by using `builtins.getFlake`.
C API consumers and language bindings now have access to basic locking functionality.

It does not expose the full locking API, so that the implementation can evolve more freely.
Locking is controlled with the functions, which cover the common use cases for consuming a flake:
- `nix_flake_lock_flags_set_mode_check`
- `nix_flake_lock_flags_set_mode_virtual`
- `nix_flake_lock_flags_set_mode_write_as_needed`
- `nix_flake_lock_flags_add_input_override`, which also enables `virtual`

This change also introduces the new `nix-fetchers-c` library, whose single purpose for now is to manage the (`nix.conf`) settings for the built-in fetchers.

More details can be found in the [C API documentation](@docroot@/c-api.md).
