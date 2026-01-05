---
synopsis: "C API: New store API methods"
prs: [14766]
---

The C API now includes additional methods:

- `nix_store_query_path_from_hash_part()` - Get the full store path given its hash part
- `nix_store_query_path_info_json()` - Get the JSON representation of a path's info.
