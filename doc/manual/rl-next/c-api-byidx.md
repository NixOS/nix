---
synopsis: "C API: `nix_get_attr_name_byidx`, `nix_get_attr_byidx` take a `nix_value *` instead of `const nix_value *`"
prs: [13987]
---

In order to accommodate a more optimized internal representation of attribute set merges these functions require
a mutable `nix_value *` that might be modified on access. This does *not* break the ABI of these functions.
