# Nix lib store build file
#============================================================================




# src files
#============================================================================

src_inc += include_directories('.')

libutil_src = files(
    join_paths(meson.source_root(), 'src/libutil/affinity.cc'),
    join_paths(meson.source_root(), 'src/libutil/archive.cc'),
    join_paths(meson.source_root(), 'src/libutil/args.cc'),
    join_paths(meson.source_root(), 'src/libutil/compression.cc'),
    join_paths(meson.source_root(), 'src/libutil/config.cc'),
    join_paths(meson.source_root(), 'src/libutil/hash.cc'),
    join_paths(meson.source_root(), 'src/libutil/json.cc'),
    join_paths(meson.source_root(), 'src/libutil/logging.cc'),
    join_paths(meson.source_root(), 'src/libutil/serialise.cc'),
    join_paths(meson.source_root(), 'src/libutil/thread-pool.cc'),
    join_paths(meson.source_root(), 'src/libutil/util.cc'),
    join_paths(meson.source_root(), 'src/libutil/xml-writer.cc'))

libutil_headers = files(
    join_paths(meson.source_root(), 'src/libutil/affinity.hh'),
    join_paths(meson.source_root(), 'src/libutil/archive.hh'),
    join_paths(meson.source_root(), 'src/libutil/args.hh'),
    join_paths(meson.source_root(), 'src/libutil/compression.hh'),
    join_paths(meson.source_root(), 'src/libutil/config.hh'),
    join_paths(meson.source_root(), 'src/libutil/finally.hh'),
    join_paths(meson.source_root(), 'src/libutil/hash.hh'),
    join_paths(meson.source_root(), 'src/libutil/istringstream_nocopy.hh'),
    join_paths(meson.source_root(), 'src/libutil/json.hh'),
    join_paths(meson.source_root(), 'src/libutil/lazy.hh'),
    join_paths(meson.source_root(), 'src/libutil/logging.hh'),
    join_paths(meson.source_root(), 'src/libutil/lru-cache.hh'),
    join_paths(meson.source_root(), 'src/libutil/monitor-fd.hh'),
    join_paths(meson.source_root(), 'src/libutil/pool.hh'),
    join_paths(meson.source_root(), 'src/libutil/ref.hh'),
    join_paths(meson.source_root(), 'src/libutil/serialise.hh'),
    join_paths(meson.source_root(), 'src/libutil/sync.hh'),
    join_paths(meson.source_root(), 'src/libutil/thread-pool.hh'),
    join_paths(meson.source_root(), 'src/libutil/types.hh'),
    join_paths(meson.source_root(), 'src/libutil/util.hh'),
    join_paths(meson.source_root(), 'src/libutil/xml-writer.hh')
)



# dependancies
#============================================================================

libutil_dep_list = [
    boost_dep,
    libbz2_dep,
    liblzma_dep,
    libbrotli_dep,
    openssl_dep,
    pthread_dep,
    libsodium_dep]




# Link args
#============================================================================

libutil_link_list = []

libutil_link_args = []




# compiler args
#============================================================================

libutil_cxx_args = []




# targets
#============================================================================




# build
#============================================================================

libutil_lib = library(
    'nixutil',
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : src_inc,
    sources : libutil_src,
    link_args : libutil_link_args,
    dependencies : libutil_dep_list)

install_headers(
    libutil_headers,
    install_dir : join_paths(includedir, 'nix'))
