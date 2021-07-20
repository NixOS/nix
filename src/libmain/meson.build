# libmain build file
#============================================================================

libmain_dir = meson.current_source_dir()
libmain_inc = []


# dependencies
#============================================================================

libmain_dep_list = [
    pthread_dep,
    openssl_dep,
    libsodium_dep,

    libutil_dep,
    libstore_dep]


# src files
#============================================================================

libmain_src = files(
    join_paths(libmain_dir, 'common-args.cc'),
    join_paths(libmain_dir, 'loggers.cc'),
    join_paths(libmain_dir, 'progress-bar.cc'),
    join_paths(libmain_dir, 'shared.cc'),
    join_paths(libmain_dir, 'stack.cc'))


libmain_headers = files(
    join_paths(libmain_dir, 'common-args.hh'),
    join_paths(libmain_dir, 'loggers.hh'),
    join_paths(libmain_dir, 'progress-bar.hh'),
    join_paths(libmain_dir, 'shared.hh'))


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libmain_inc += include_directories('.')


# targets
#============================================================================


# build
#============================================================================

# set build args
#---------------------------------------------------
libmain_cxx_args = []

libmain_link_args = []


# build library
#---------------------------------------------------
libmain_lib = library(
    'nixmain',
    sources : libmain_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libmain_inc,
        proj_inc],
    cpp_args : libmain_cxx_args,
    link_args : libmain_link_args,
    dependencies : libmain_dep_list)


# install headers
#---------------------------------------------------
install_headers(
    libmain_headers,
    install_dir : join_paths(includedir, 'nix'))

# declare dependency
#---------------------------------------------------
libmain_dep = declare_dependency(
    link_with : libmain_lib,
    include_directories : libmain_inc)
