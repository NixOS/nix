---
synopsis: "C API: Add store path metadata accessors"
prs: [15675]
---

The C API now includes functions for querying store path metadata:

- `nix_store_query_path_info()` - Query metadata for a store path
- `nix_path_info_get_nar_hash()` - Get the NAR hash
- `nix_path_info_get_nar_size()` - Get the NAR size
- `nix_path_info_get_references()` - Iterate over references
- `nix_path_info_get_deriver()` - Get the deriver
- `nix_path_info_get_sigs()` - Iterate over signatures
- `nix_path_info_get_ca()` - Get the content address
- `nix_path_info_free()` - Free store path metadata
