# libstore build file
#============================================================================

libstore_dir =  meson.current_source_dir()
libstore_inc = [include_directories('.')]

# dependencies
#============================================================================

libstore_dep_list = [
    aws_sdk_cpp_dep,
    libbz2_dep,
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
    join_paths(libstore_dir, 'binary-cache-store.cc'),
    join_paths(libstore_dir, 'content-address.cc'),
    join_paths(libstore_dir, 'crypto.cc'),
    join_paths(libstore_dir, 'daemon.cc'),
    join_paths(libstore_dir, 'derivations.cc'),
    join_paths(libstore_dir, 'derived-path.cc'),
    join_paths(libstore_dir, 'dummy-store.cc'),
    join_paths(libstore_dir, 'export-import.cc'),
    join_paths(libstore_dir, 'filetransfer.cc'),
    join_paths(libstore_dir, 'gc.cc'),
    join_paths(libstore_dir, 'globals.cc'),
    join_paths(libstore_dir, 'http-binary-cache-store.cc'),
    join_paths(libstore_dir, 'legacy-ssh-store.cc'),
    join_paths(libstore_dir, 'local-binary-cache-store.cc'),
    join_paths(libstore_dir, 'local-fs-store.cc'),
    join_paths(libstore_dir, 'local-store.cc'),
    join_paths(libstore_dir, 'lock.cc'),
    join_paths(libstore_dir, 'machines.cc'),
    join_paths(libstore_dir, 'misc.cc'),
    join_paths(libstore_dir, 'names.cc'),
    join_paths(libstore_dir, 'nar-accessor.cc'),
    join_paths(libstore_dir, 'nar-info.cc'),
    join_paths(libstore_dir, 'nar-info-disk-cache.cc'),
    join_paths(libstore_dir, 'optimise-store.cc'),
    join_paths(libstore_dir, 'parsed-derivations.cc'),
    join_paths(libstore_dir, 'path.cc'),
    join_paths(libstore_dir, 'pathlocks.cc'),
    join_paths(libstore_dir, 'path-with-outputs.cc'),
    join_paths(libstore_dir, 'profiles.cc'),
    join_paths(libstore_dir, 'realisation.cc'),
    join_paths(libstore_dir, 'references.cc'),
    join_paths(libstore_dir, 'remote-fs-accessor.cc'),
    join_paths(libstore_dir, 'remote-store.cc'),
    join_paths(libstore_dir, 's3-binary-cache-store.cc'),
    join_paths(libstore_dir, 'sqlite.cc'),
    join_paths(libstore_dir, 'ssh.cc'),
    join_paths(libstore_dir, 'ssh-store.cc'),
    join_paths(libstore_dir, 'store-api.cc'),
    join_paths(libstore_dir, 'uds-remote-store.cc'))

libstore_headers = files(
    join_paths(libstore_dir, 'binary-cache-store.hh'),
    join_paths(libstore_dir, 'builtins.hh'),
    join_paths(libstore_dir, 'content-address.hh'),
    join_paths(libstore_dir, 'crypto.hh'),
    join_paths(libstore_dir, 'daemon.hh'),
    join_paths(libstore_dir, 'derivations.hh'),
    join_paths(libstore_dir, 'derived-path.hh'),
    join_paths(libstore_dir, 'filetransfer.hh'),
    join_paths(libstore_dir, 'fs-accessor.hh'),
    join_paths(libstore_dir, 'globals.hh'),
    join_paths(libstore_dir, 'local-fs-store.hh'),
    join_paths(libstore_dir, 'local-store.hh'),
    join_paths(libstore_dir, 'lock.hh'),
    join_paths(libstore_dir, 'machines.hh'),
    join_paths(libstore_dir, 'names.hh'),
    join_paths(libstore_dir, 'nar-accessor.hh'),
    join_paths(libstore_dir, 'nar-info-disk-cache.hh'),
    join_paths(libstore_dir, 'nar-info.hh'),
    join_paths(libstore_dir, 'parsed-derivations.hh'),
    join_paths(libstore_dir, 'path.hh'),
    join_paths(libstore_dir, 'path-info.hh'),
    join_paths(libstore_dir, 'pathlocks.hh'),
    join_paths(libstore_dir, 'path-with-outputs.hh'),
    join_paths(libstore_dir, 'profiles.hh'),
    join_paths(libstore_dir, 'realisation.hh'),
    join_paths(libstore_dir, 'references.hh'),
    join_paths(libstore_dir, 'remote-fs-accessor.hh'),
    join_paths(libstore_dir, 'remote-store.hh'),
    join_paths(libstore_dir, 's3-binary-cache-store.hh'),
    join_paths(libstore_dir, 's3.hh'),
    join_paths(libstore_dir, 'serve-protocol.hh'),
    join_paths(libstore_dir, 'sqlite.hh'),
    join_paths(libstore_dir, 'ssh.hh'),
    join_paths(libstore_dir, 'store-api.hh'),
    join_paths(libstore_dir, 'uds-remote-store.hh'),
    join_paths(libstore_dir, 'worker-protocol.hh'))


libstore_data = files(
    join_paths(libstore_dir, 'sandbox-defaults.sb'),
    join_paths(libstore_dir, 'sandbox-minimal.sb'),
    join_paths(libstore_dir, 'sandbox-network.sb'))


# include directories
#========================================================================

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
    '-DLSOF="@0@" '.format(lsof)]

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
libstore_config = pkg.generate(
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
