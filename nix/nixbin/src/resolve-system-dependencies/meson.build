# resolve-system-dependencies build file
#============================================================================

resolvesysdeps_inc = [include_directories('.')]

# dependencies
#============================================================================

resolvesysdeps_dep_list = []


# src files
#============================================================================

resolvesysdeps_src = files(
    'resolve-system-dependencies.cc')


# build
#============================================================================

# set build args
#---------------------------------------------------
resolvesysdeps_cxx_args = []

resolvesysdeps_link_args = []

# build executable
#---------------------------------------------------
resolvesysdeps_bin = executable(
    'resolve-system-dependencies',
    sources : resolvesysdeps_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : bindir,
    include_directories : proj_inc,
    cpp_args : resolvesysdeps_cxx_args,
    link_args : resolvesysdeps_link_args,
    dependencies : resolvesysdeps_dep_list)
