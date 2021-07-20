# libutil build file
#============================================================================

libutil_dir = meson.current_source_dir()
libutil_inc = [include_directories('.')]


# dependencies
#============================================================================

libutil_dep_list = [
    aws_sdk_cpp_dep,
    boost_dep,
    libarchive_dep,
    libbz2_dep,
    libbrotli_dep,
    libcpuid_dep,
    liblzma_dep,
    libsodium_dep,
    nlohmann_dep,
    openssl_dep,
    pthread_dep,
    zlib_dep
    ]


# src files
#============================================================================

libutil_src = files(
    join_paths(libutil_dir, 'affinity.cc'),
    join_paths(libutil_dir, 'archive.cc'),
    join_paths(libutil_dir, 'args.cc'),
    join_paths(libutil_dir, 'compression.cc'),
    join_paths(libutil_dir, 'compute-levels.cc'),
    join_paths(libutil_dir, 'config.cc'),
    join_paths(libutil_dir, 'error.cc'),
    join_paths(libutil_dir, 'hash.cc'),
    join_paths(libutil_dir, 'json.cc'),
    join_paths(libutil_dir, 'logging.cc'),
    join_paths(libutil_dir, 'rust-ffi.cc'),
    join_paths(libutil_dir, 'serialise.cc'),
    join_paths(libutil_dir, 'tarfile.cc'),
    join_paths(libutil_dir, 'thread-pool.cc'),
    join_paths(libutil_dir, 'url.cc'),
    join_paths(libutil_dir, 'util.cc'),
    join_paths(libutil_dir, 'xml-writer.cc'))

libutil_headers = files(
    join_paths(libutil_dir, 'abstract-setting-to-json.hh'),
    join_paths(libutil_dir, 'affinity.hh'),
    join_paths(libutil_dir, 'ansicolor.hh'),
    join_paths(libutil_dir, 'archive.hh'),
    join_paths(libutil_dir, 'args.hh'),
    join_paths(libutil_dir, 'callback.hh'),
    join_paths(libutil_dir, 'comparator.hh'),
    join_paths(libutil_dir, 'compression.hh'),
    join_paths(libutil_dir, 'compute-levels.hh'),
    join_paths(libutil_dir, 'config.hh'),
    join_paths(libutil_dir, 'error.hh'),
    join_paths(libutil_dir, 'finally.hh'),
    join_paths(libutil_dir, 'fmt.hh'),
    join_paths(libutil_dir, 'hash.hh'),
    join_paths(libutil_dir, 'json.hh'),
    join_paths(libutil_dir, 'logging.hh'),
    join_paths(libutil_dir, 'lru-cache.hh'),
    join_paths(libutil_dir, 'monitor-fd.hh'),
    join_paths(libutil_dir, 'pool.hh'),
    join_paths(libutil_dir, 'ref.hh'),
    join_paths(libutil_dir, 'rust-ffi.hh'),
    join_paths(libutil_dir, 'serialise.hh'),
    join_paths(libutil_dir, 'split.hh'),
    join_paths(libutil_dir, 'sync.hh'),
    join_paths(libutil_dir, 'tarfile.hh'),
    join_paths(libutil_dir, 'thread-pool.hh'),
    join_paths(libutil_dir, 'topo-sort.hh'),
    join_paths(libutil_dir, 'types.hh'),
    join_paths(libutil_dir, 'url.hh'),
    join_paths(libutil_dir, 'url-parts.hh'),
    join_paths(libutil_dir, 'util.hh'),
    join_paths(libutil_dir, 'xml-writer.hh'))
    

# build
#============================================================================

# set build args
#---------------------------------------------------
libutil_cxx_args = []

libutil_link_args = []


# build library
#---------------------------------------------------
libutil_lib = library(
    'nixutil',
    sources : libutil_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libutil_inc,
        proj_inc],
    cpp_args : libutil_cxx_args,
    link_args : libutil_link_args,
    dependencies : libutil_dep_list)


# install headers
#---------------------------------------------------
install_headers(
    libutil_headers,
    install_dir : join_paths(includedir, 'nix'))


# declare dependency
#---------------------------------------------------
libutil_dep = declare_dependency(
    link_with : libutil_lib,
    include_directories : libutil_inc)


# FIXME!!! tests should not be declared inside the
# build directory, but should be moved to a seperate test
# directory. for now, we just include it here.


# add test dir
#---------------------------------------------------
subdir('tests')
