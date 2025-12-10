---
synopsis: "C API: New store API methods"
prs: [14766]
---

The C API now includes additional methods:

- `nix_store_query_path_from_hash_part()` - Get the full store path given its hash part
- `nix_store_copy_path()` - Copy a single store path between two stores, allows repairs and configuring signature checking
