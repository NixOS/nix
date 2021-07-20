# libfetchers build file
#============================================================================

libfetchers_dir =  meson.current_source_dir()
libfetchers_inc = []

# dependencies
#============================================================================

libfetchers_dep_list = [
    libbz2_dep,
    libcurl_dep,
    libdl_dep,
    libseccomp_dep,
    libsodium_dep,
    #nlohmann_dep,
    pthread_dep,
    sqlite3_dep,

    libutil_dep,
    libstore_dep]


# src files
#============================================================================

libfetchers_src = files(
    join_paths(libfetchers_dir, 'attrs.cc'),
    join_paths(libfetchers_dir, 'cache.cc'),
    join_paths(libfetchers_dir, 'fetchers.cc'),
    join_paths(libfetchers_dir, 'git.cc'),
    join_paths(libfetchers_dir, 'github.cc'),
    join_paths(libfetchers_dir, 'indirect.cc'),
    join_paths(libfetchers_dir, 'mercurial.cc'),
    join_paths(libfetchers_dir, 'path.cc'),
    join_paths(libfetchers_dir, 'registry.cc'),
    join_paths(libfetchers_dir, 'tarball.cc'))

libfetchers_headers = files(
    join_paths(libfetchers_dir, 'attrs.hh'),
    join_paths(libfetchers_dir, 'cache.hh'),
    join_paths(libfetchers_dir, 'fetchers.hh'),
    join_paths(libfetchers_dir, 'registry.hh'))


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libfetchers_inc += include_directories('.')


# targets
#============================================================================


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
libfetchers_config = pkg.generate(
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
