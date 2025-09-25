---
synopsis: "C API: Add lazy attribute and list item accessors"
prs: [14030]
---

The C API now includes lazy accessor functions for retrieving values from lists and attribute sets without forcing evaluation:

- `nix_get_list_byidx_lazy()` - Get a list element without forcing its evaluation
- `nix_get_attr_byname_lazy()` - Get an attribute value by name without forcing evaluation
- `nix_get_attr_byidx_lazy()` - Get an attribute by index without forcing evaluation

These functions are useful when forwarding unevaluated sub-values to other lists, attribute sets, or function calls. They allow more efficient handling of Nix values by deferring evaluation until actually needed.

Additionally, bounds checking has been improved for all `_byidx` functions to properly validate indices before access, preventing potential out-of-bounds errors.

The documentation for `NIX_ERR_KEY` error handling has also been clarified to specify when this error code is returned.