# nlohmann-json build file
#============================================================================

nlohmann_inc = [include_directories('.')]

nlohmann_headers = files(
    'json.hpp',
    'json_fwd.hpp',
)

install_headers(
    nlohmann_headers,
    install_dir : join_paths(includedir, 'nix'))

# declare dependency
#---------------------------------------------------
nlohmann_dep = declare_dependency(
    include_directories : nlohmann_inc)
