# libstore build file
#============================================================================


# dependencies
#============================================================================

libstore_dep_list = [
    aws_sdk_cpp_dep,
    libcurl_dep,
    libdl_dep,
    libseccomp_dep,
    libsodium_dep,
    nlohmann_dep,
    pthread_dep,
    sqlite3_dep,


    libutil_dep]


# src files
#============================================================================

libstore_src = files(
    'binary-cache-store.cc',
    'build-result.cc',
    'common-protocol.cc',
    'content-address.cc',
    'daemon.cc',
    'derivations.cc',
    'derived-path.cc',
    'derived-path-map.cc',
    'downstream-placeholder.cc',
    'dummy-store.cc',
    'export-import.cc',
    'filetransfer.cc',
    'gc.cc',
    'globals.cc',
    'http-binary-cache-store.cc',
    'keys.cc',
    'legacy-ssh-store.cc',
    'local-binary-cache-store.cc',
    'local-fs-store.cc',
    'local-store.cc',
    'lock.cc',
    'log-store.cc',
    'machines.cc',
    'make-content-addressed.cc',
    'misc.cc',
    'names.cc',
    'nar-accessor.cc',
    'nar-info.cc',
    'nar-info-disk-cache.cc',
    'optimise-store.cc',
    'outputs-spec.cc',
    'parsed-derivations.cc',
    'path.cc',
    'path-info.cc',
    'pathlocks.cc',
    'path-references.cc',
    'path-with-outputs.cc',
    'posix-fs-canonicalise.cc',
    'profiles.cc',
    'realisation.cc',
    'remote-fs-accessor.cc',
    'remote-store.cc',
    's3-binary-cache-store.cc',
    'serve-protocol.cc',
    'serve-protocol-impl.cc',
    'sqlite.cc',
    'ssh.cc',
    'ssh-store.cc',
    'store-api.cc',
    'uds-remote-store.cc',
    'worker-protocol.cc')


libstore_headers = files(
    'binary-cache-store.hh',
    'build-result.hh',
    'builtins.hh',
    'common-protocol.hh',
    'common-protocol-impl.hh',
    'content-address.hh',
    'daemon.hh',
    'derivations.hh',
    'derived-path.hh',
    'derived-path-map.hh',
    'downstream-placeholder.hh',
    'filetransfer.hh',
    'gc-store.hh',
    'globals.hh',
    'indirect-root-store.hh',
    'keys.hh',
    'legacy-ssh-store.hh',
    'length-prefixed-protocol-helper.hh',
    'local-fs-store.hh',
    'local-store.hh',
    'lock.hh',
    'log-store.hh',
    'machines.hh',
    'make-content-addressed.hh',
    'names.hh',
    'nar-accessor.hh',
    'nar-info-disk-cache.hh',
    'nar-info.hh',
    'outputs-spec.hh',
    'parsed-derivations.hh',
    'path.hh',
    'path-info.hh',
    'pathlocks.hh',
    'path-references.hh',
    'path-regex.hh',
    'path-with-outputs.hh',
    'posix-fs-canonicalise.hh',
    'profiles.hh',
    'realisation.hh',
    'remote-fs-accessor.hh',
    'remote-store-connection.hh',
    'remote-store.hh',
    's3-binary-cache-store.hh',
    's3.hh',
    'serve-protocol.hh',
    'serve-protocol-impl.hh',
    'sqlite.hh',
    'ssh.hh',
    'ssh-store-config.hh',
    'store-api.hh',
    'store-cast.hh',
    'store-dir-config.hh',
    'uds-remote-store.hh',
    'worker-protocol.hh',
    'worker-protocol-impl.hh')

libstore_data = files()


# include directories
#========================================================================

# include current dir
#---------------------------------------------------
libstore_inc = [include_directories('.')]

libstore_dirs = [
    'build',
    'builtins']

foreach dir : libstore_dirs
    subdir(dir)
endforeach


# targets
#============================================================================

libstore_src += custom_target(
  'schema.sql.gen.hh',
  output : 'schema.sql.gen.hh',
  input : 'schema.sql',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])


libstore_src += custom_target(
  'ca-specific-schema.sql.gen.hh',
  output : 'ca-specific-schema.sql.gen.hh',
  input : 'ca-specific-schema.sql',
  command : [bash, '-c', gen_rheader, 'sh', '@OUTPUT@'])


# build
#============================================================================

# set build args
#---------------------------------------------------
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
    '-DLSOF="@0@" '.format(lsof.full_path())]

libstore_link_args = []


# build library
#---------------------------------------------------
libstore_lib = library(
    'nixstore',
    sources : libstore_src,
    install : true,
    install_mode : 'rwxr-xr-x',
    install_dir : libdir,
    include_directories : [
        libstore_inc,
        proj_inc],
    cpp_args : libstore_cxx_args,
    link_args : libstore_link_args,
    dependencies : libstore_dep_list)


# install headers
#---------------------------------------------------
install_headers(
    libstore_headers,
    install_dir : join_paths(includedir, 'nix'))


# install data
#---------------------------------------------------
install_data(
    libstore_data,
    install_dir : join_paths(datadir, 'nix/sandbox'))


# generate pkg-config
#---------------------------------------------------
libstore_config = pkgconfig.generate(
    libstore_lib,
    libraries : [
        libstore_lib],
    version : meson.project_version(),
    name : 'Nix',
    subdirs : ['nix/'],
    filebase : 'nix-store',
    extra_cflags : '-std=c++17',
    description : 'Nix Package Manager.')


# declare dependency
#---------------------------------------------------
libstore_dep = declare_dependency(
    link_with : libstore_lib,
    include_directories : libstore_inc)
