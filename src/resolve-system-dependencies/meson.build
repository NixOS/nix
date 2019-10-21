# Nix exec build file
#============================================================================




# src files
#============================================================================

nix_src = files(
    join_paths(meson.source_root(),
    'src/resolve-system-dependencies/resolve-system-dependencies.cc'))



# dependancies
#============================================================================

sys_deps_dep_list = []




# Link args
#============================================================================

sys_deps_link_args = []




# compiler args
#============================================================================

sys_deps_cxx_args = []




# targets
#============================================================================




# build
#============================================================================

if sys_name == 'darwin'
    sys_deps_bin = executable(
        'resolve-system-dependencies',
        install : true,
        install_dir : bindir,
        include_directories : src_inc,
        sources : nix_src,
        link_args : sys_deps_link_args,
        dependencies : sys_deps_dep_list)
endif