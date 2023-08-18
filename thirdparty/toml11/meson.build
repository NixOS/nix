# toml11 build file
#============================================================================

toml11_inc = [include_directories('.')]

toml11_headers = files(
    join_paths('toml11', 'toml.hpp'))

install_headers(
    toml11_headers,
    install_dir : join_paths(includedir, 'nix'))

# declare dependency
#---------------------------------------------------
toml11_dep = declare_dependency(
    include_directories : toml11_inc)
