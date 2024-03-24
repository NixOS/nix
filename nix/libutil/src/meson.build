# libutil build file
#============================================================================


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
    'current-process.cc',
    'english.cc',
    'environment-variables.cc',
    'error.cc',
    'exit.cc',
    'experimental-features.cc',
    'file-content-address.cc',
    'file-descriptor.cc',
    'file-system.cc',
    'fs-sink.cc',
    'git.cc',
    'hash.cc',
    'hilite.cc',
    'json-utils.cc',
    'logging.cc',
    'memory-source-accessor.cc',
    'namespaces.cc',
    'position.cc',
    'posix-source-accessor.cc',
    'processes.cc',
    'references.cc',
    'serialise.cc',
    'signals.cc',
    'source-accessor.cc',
    'source-path.cc',
    'suggestions.cc',
    'tarfile.cc',
    'terminal.cc',
    'thread-pool.cc',
    'unix-domain-socket.cc',
    'url.cc',
    'users.cc',
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
    'current-process.hh',
    'english.hh',
    'environment-variables.hh',
    'error.hh',
    'exit.hh',
    'experimental-features.hh',
    'file-content-address.hh',
    'file-descriptor.hh',
    'file-path-impl.hh',
    'file-system.hh',
    'finally.hh',
    'fmt.hh',
    'fs-sink.hh',
    'git.hh',
    'hash.hh',
    'hilite.hh',
    'input-accessor.hh',
    'json-impls.hh',
    'json-utils.hh',
    'logging.hh',
    'lru-cache.hh',
    'memory-source-accessor.hh',
    'monitor-fd.hh',
    'namespaces.hh',
    'pool.hh',
    'position.hh',
    'posix-source-accessor.hh',
    'processes.hh',
    'references.hh',
    'ref.hh',
    'regex-combinators.hh',
    'repair-flag.hh',
    'serialise.hh',
    'signals.hh',
    'source-accessor.hh',
    'source-path.hh',
    'split.hh',
    'suggestions.hh',
    'sync.hh',
    'tarfile.hh',
    'terminal.hh',
    'thread-pool.hh',
    'topo-sort.hh',
    'types.hh',
    'unix-domain-socket.hh',
    'url.hh',
    'url-parts.hh',
    'users.hh',
    'util.hh',
    'variant-wrapper.hh',
    'xml-writer.hh')

# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libutil_inc = [include_directories('.')]

libutil_dirs = [
    'args',
    'signature']

foreach dir : libutil_dirs
    subdir(dir)
endforeach



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
    include_directories : proj_inc,
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
