# libcmd build file
#============================================================================

libcmd_inc = [include_directories('.')]

# dependencies
#============================================================================

libcmd_dep_list = [
    gc_dep,
    editline_dep,
    libsodium_dep,
    liblowdown_dep,
    nlohmann_dep,
    openssl_dep,
    pthread_dep,
    
    libutil_dep,
    libmain_dep,
    libstore_dep,
    libexpr_dep,
    libfetchers_dep]


# src files
#============================================================================

libcmd_src = files(
    'built-path.cc',
    'command.cc',
    'command-installable-value.cc',
    'common-eval-args.cc',
    'editor-for.cc',
    'installable-attr-path.cc',
    'installable-derived-path.cc',
    'installable-flake.cc',
    'installables.cc',
    'installable-value.cc',
    'legacy.cc',
    'markdown.cc',
    'misc-store-flags.cc',
    'repl.cc')

libcmd_headers = files(
    'built-path.hh',
    'command.hh',
    'command-installable-value.hh',
    'common-eval-args.hh',
    'editor-for.hh',
    'installable-attr-path.hh',
    'installable-derived-path.hh',
    'installable-flake.hh',
    'installables.hh',
    'installable-value.hh',
    'legacy.hh',
    'markdown.hh',
    'misc-store-flags.hh',
    'repl.hh')

# set build args
#---------------------------------------------------
nix_cxx_args = []
libcmd_cxx_args = []

nix_link_args = []
libcmd_link_args = []

# build library
#---------------------------------------------------
libcmd_lib = library(
    'nixcmd',
    sources : libcmd_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libcmd_inc,
        proj_inc],
    cpp_args : libcmd_cxx_args,
    link_args : libcmd_link_args,
    dependencies : libcmd_dep_list)

# install headers
#---------------------------------------------------
install_headers(
    libcmd_headers,
    install_dir : join_paths(includedir, 'nix'))

# declare dependency
#---------------------------------------------------
libcmd_dep = declare_dependency(
    link_with : libcmd_lib,
    include_directories : libcmd_inc)
