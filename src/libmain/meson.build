# Nix lib store build file
#============================================================================




# src files
#============================================================================

src_inc += include_directories('.')

libmain_src = files(
    join_paths(meson.source_root(), 'src/libmain/common-args.cc'),
    join_paths(meson.source_root(), 'src/libmain/shared.cc'),
    join_paths(meson.source_root(), 'src/libmain/stack.cc'))

libmain_headers = files(
    join_paths(meson.source_root(), 'src/libmain/common-args.hh'),
    join_paths(meson.source_root(), 'src/libmain/shared.hh'))




# dependancies
#============================================================================

libmain_dep_list = [
    pthread_dep,
    openssl_dep,
    libsodium_dep]





# Link args
#============================================================================

libmain_link_list = [
    libutil_lib,
    libstore_lib
]

libmain_link_args = []




# compiler args
#============================================================================

libstore_cxx_args = []




# targets
#============================================================================




# build
#============================================================================

libmain_lib = library(
    'nixmain',
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : src_inc,
    link_with : libmain_link_list,
    sources : libmain_src,
    link_args : libmain_link_args,
    dependencies : libmain_dep_list)

install_headers(
    libmain_headers,
    install_dir : join_paths(includedir, 'nix'))
