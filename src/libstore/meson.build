# Nix lib store build file
#============================================================================




# src files
#============================================================================

src_inc += include_directories('.')

libstore_src = files(
    join_paths(meson.source_root(), 'src/libstore/binary-cache-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/build.cc'),
    join_paths(meson.source_root(), 'src/libstore/crypto.cc'),
    join_paths(meson.source_root(), 'src/libstore/derivations.cc'),
    join_paths(meson.source_root(), 'src/libstore/download.cc'),
    join_paths(meson.source_root(), 'src/libstore/export-import.cc'),
    join_paths(meson.source_root(), 'src/libstore/gc.cc'),
    join_paths(meson.source_root(), 'src/libstore/globals.cc'),
    join_paths(meson.source_root(), 'src/libstore/http-binary-cache-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/legacy-ssh-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/local-binary-cache-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/local-fs-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/local-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/machines.cc'),
    join_paths(meson.source_root(), 'src/libstore/misc.cc'),
    join_paths(meson.source_root(), 'src/libstore/nar-accessor.cc'),
    join_paths(meson.source_root(), 'src/libstore/nar-info.cc'),
    join_paths(meson.source_root(), 'src/libstore/nar-info-disk-cache.cc'),
    join_paths(meson.source_root(), 'src/libstore/optimise-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/parsed-derivations.cc'),
    join_paths(meson.source_root(), 'src/libstore/pathlocks.cc'),
    join_paths(meson.source_root(), 'src/libstore/profiles.cc'),
    join_paths(meson.source_root(), 'src/libstore/references.cc'),
    join_paths(meson.source_root(), 'src/libstore/remote-fs-accessor.cc'),
    join_paths(meson.source_root(), 'src/libstore/remote-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/s3-binary-cache-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/sqlite.cc'),
    join_paths(meson.source_root(), 'src/libstore/ssh.cc'),
    join_paths(meson.source_root(), 'src/libstore/ssh-store.cc'),
    join_paths(meson.source_root(), 'src/libstore/store-api.cc'),
    join_paths(meson.source_root(), 'src/libstore/builtins/buildenv.cc'),
    join_paths(meson.source_root(), 'src/libstore/builtins/fetchurl.cc'))

libstore_headers = files(
    join_paths(meson.source_root(), 'src/libstore/binary-cache-store.hh'),
    join_paths(meson.source_root(), 'src/libstore/builtins.hh'),
    join_paths(meson.source_root(), 'src/libstore/crypto.hh'),
    join_paths(meson.source_root(), 'src/libstore/derivations.hh'),
    join_paths(meson.source_root(), 'src/libstore/download.hh'),
    join_paths(meson.source_root(), 'src/libstore/fs-accessor.hh'),
    join_paths(meson.source_root(), 'src/libstore/globals.hh'),
    join_paths(meson.source_root(), 'src/libstore/local-store.hh'),
    join_paths(meson.source_root(), 'src/libstore/machines.hh'),
    join_paths(meson.source_root(), 'src/libstore/nar-accessor.hh'),
    join_paths(meson.source_root(), 'src/libstore/nar-info-disk-cache.hh'),
    join_paths(meson.source_root(), 'src/libstore/nar-info.hh'),
    join_paths(meson.source_root(), 'src/libstore/parsed-derivations.hh'),
    join_paths(meson.source_root(), 'src/libstore/pathlocks.hh'),
    join_paths(meson.source_root(), 'src/libstore/profiles.hh'),
    join_paths(meson.source_root(), 'src/libstore/references.hh'),
    join_paths(meson.source_root(), 'src/libstore/remote-fs-accessor.hh'),
    join_paths(meson.source_root(), 'src/libstore/remote-store.hh'),
    join_paths(meson.source_root(), 'src/libstore/s3-binary-cache-store.hh'),
    join_paths(meson.source_root(), 'src/libstore/s3.hh'),
    join_paths(meson.source_root(), 'src/libstore/serve-protocol.hh'),
    join_paths(meson.source_root(), 'src/libstore/sqlite.hh'),
    join_paths(meson.source_root(), 'src/libstore/ssh.hh'),
    join_paths(meson.source_root(), 'src/libstore/store-api.hh'),
    join_paths(meson.source_root(), 'src/libstore/worker-protocol.hh'))

libstore_data = files(
    join_paths(meson.source_root(), 'src/libstore/sandbox-defaults.sb'),
    join_paths(meson.source_root(), 'src/libstore/sandbox-minimal.sb'),
    join_paths(meson.source_root(), 'src/libstore/sandbox-network.sb'))

# dependancies
#============================================================================

libstore_dep_list = [
    libbz2_dep,
    libcurl_dep,
    libdl_dep,
    pthread_dep,
    sqlite3_dep,
    libsodium_dep
]

if sys_name.contains('linux')
    libstore_dep_list += libseccomp_dep
endif

if sys_name.contains('freebsd')
    libstore_dep_list += libdl_dep
endif




# Link args
#============================================================================

libstore_link_list = [
    libutil_lib]

libstore_link_args = []




# compiler args
#============================================================================

libstore_cxx_args = [
    '-DNIX_PREFIX="@0@" '.format(prefix),
    '-DNIX_STORE_DIR="@0@" '.format(nixstoredir),
    '-DNIX_DATA_DIR="@0@" '.format(datadir),
    '-DNIX_STATE_DIR="@0@" '.format(join_paths(localstatedir, 'nix')),
    '-DNIX_LOG_DIR="@0@" '.format(join_paths(localstatedir, 'log/nix')),
    '-DNIX_CONF_DIR="@0@" '.format(join_paths(sysconfdir, 'nix')),
    '-DNIX_LIBEXEC_DIR="@0@" '.format(libexecdir),
    '-DNIX_BIN_DIR="@0@" '.format(bindir),
    '-DNIX_MAN_DIR="@0@" '.format(mandir),
    '-DSANDBOX_SHELL="@0@" '.format(get_option('sandbox_shell')),
    '-DLSOF="@0@" '.format(lsof)]




# targets
#============================================================================

gen_header = '''
  echo 'R"foo(' >> "$1"
  cat @INPUT@ >> "$1"
  echo ')foo"' >> "$1"
'''

libstore_src += custom_target(
  'schema.sql.gen.hh',
  output : 'schema.sql.gen.hh',
  input : 'schema.sql',
  command : [bash, '-c', gen_header, 'sh', '@OUTPUT@'])




# build
#============================================================================

libstore_lib = library(
    'nixstore',
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : src_inc,
    link_with : libstore_link_list,
    sources : libstore_src,
    cpp_args : libstore_cxx_args,
    link_args : libstore_link_args,
    dependencies : libstore_dep_list)

install_headers(
    libstore_headers,
    install_dir : join_paths(includedir, 'nix'))

install_data(
    libstore_data,
    install_dir : join_paths(datadir, 'nix/sandbox'))
