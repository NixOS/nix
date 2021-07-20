# nix-store build file
#============================================================================

nix_store_dir = meson.current_source_dir()
nix_store_inc = [include_directories('.')]


# src files
#============================================================================

nix_store_src = files(
    join_paths(nix_store_dir, 'dotgraph.cc'),
    join_paths(nix_store_dir, 'graphml.cc'),
    join_paths(nix_store_dir, 'nix-store.cc'))
