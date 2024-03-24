# libfetchers build file
#============================================================================


# dependencies
#============================================================================

libfetchers_dep_list = [
    libcurl_dep,
    libdl_dep,
    libseccomp_dep,
    libsodium_dep,
    pthread_dep,
    sqlite3_dep,
    libgit_dep,

    libutil_dep,
    libstore_dep]


# src files
#============================================================================

libfetchers_src = files(
    'attrs.cc',
    'cache.cc',
    'fetchers.cc',
    'fetch-settings.cc',
    'fetch-to-store.cc',
    'filtering-input-accessor.cc',
    'fs-input-accessor.cc',
    'git.cc',
    'github.cc',
    'git-utils.cc',
    'indirect.cc',
    'memory-input-accessor.cc',
    'mercurial.cc',
    'mounted-input-accessor.cc',
    'path.cc',
    'registry.cc',
    'tarball.cc')

libfetchers_headers = files(
    'attrs.hh',
    'cache.hh',
    'fetchers.hh',
    'fetch-settings.hh',
    'fetch-to-store.hh',
    'filtering-input-accessor.hh',
    'fs-input-accessor.hh',
    'git-utils.hh',
    'memory-input-accessor.hh',
    'mounted-input-accessor.hh',
    'registry.hh',
    'tarball.hh')


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libfetchers_inc = [include_directories('.')]


# build
#============================================================================

# set build args
#---------------------------------------------------
libfetchers_cxx_args = []

libfetchers_link_args = []


# build library
#---------------------------------------------------
libfetchers_lib = library(
    'nixfetchers',
    sources : libfetchers_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libfetchers_inc,
        proj_inc],
    cpp_args : libfetchers_cxx_args,
    link_args : libfetchers_link_args,
    dependencies : libfetchers_dep_list)


# install headers
#---------------------------------------------------
install_headers(
    libfetchers_headers,
    install_dir : join_paths(includedir, 'nix'))


# generate pkg-config
#---------------------------------------------------
libfetchers_config = pkgconfig.generate(
    libfetchers_lib,
    libraries : [
        libfetchers_lib],
    version : meson.project_version(),
    name : 'Nix',
    subdirs : ['nix/'],
    filebase : 'nix-store',
    extra_cflags : '-std=c++17',
    description : 'Nix Package Manager.')


# declare dependency
#---------------------------------------------------
libfetchers_dep = declare_dependency(
    link_with : libfetchers_lib,
    include_directories : libfetchers_inc)
