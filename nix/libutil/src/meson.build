# libutil build file
#============================================================================

libutil_inc = [include_directories('.')]


# dependencies
#============================================================================

libutil_dep_list = [
    aws_sdk_cpp_dep,
    boost_dep,
    libarchive_dep,
    libbrotli_dep,
    libcpuid_dep,
    libsodium_dep,
    nlohmann_dep,
    openssl_dep,
    pthread_dep,
]


# src files
#============================================================================

libutil_src = files(
    'archive.cc',
    'args.cc',
    'canon-path.cc',
    'cgroup.cc',
    'compression.cc',
    'compute-levels.cc',
    'config.cc',
    'error.cc',
    'experimental-features.cc',
    'filesystem.cc',
    'git.cc',
    'hash.cc',
    'hilite.cc',
    'json-utils.cc',
    'logging.cc',
    'namespaces.cc',
    'references.cc',
    'serialise.cc',
    'suggestions.cc',
    'tarfile.cc',
    'thread-pool.cc',
    'url.cc',
    'util.cc',
    'xml-writer.cc')


libutil_headers = files(
    'abstract-setting-to-json.hh',
    'ansicolor.hh',
    'archive.hh',
    'args.hh',
    'callback.hh',
    'canon-path.hh',
    'cgroup.hh',
    'chunked-vector.hh',
    'closure.hh',
    'comparator.hh',
    'compression.hh',
    'compute-levels.hh',
    'config.hh',
    'config-impl.hh',
    'error.hh',
    'experimental-features.hh',
    'finally.hh',
    'fmt.hh',
    'git.hh',
    'hash.hh',
    'hilite.hh',
    'json-impls.hh',
    'json-utils.hh',
    'logging.hh',
    'lru-cache.hh',
    'monitor-fd.hh',
    'namespaces.hh',
    'pool.hh',
    'references.hh',
    'ref.hh',
    'regex-combinators.hh',
    'serialise.hh',
    'split.hh',
    'suggestions.hh',
    'sync.hh',
    'tarfile.hh',
    'thread-pool.hh',
    'topo-sort.hh',
    'types.hh',
    'url.hh',
    'url-parts.hh',
    'util.hh',
    'xml-writer.hh')


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
        proj_inc
    ],
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
