sql_header_script = '''
  echo 'R"foo(' >> @OUTPUT@
  cat @INPUT@ >> @OUTPUT@
  echo ')foo"' >> @OUTPUT@
'''

sql_header = custom_target(
  'schema.sql.gen.hh',
  output : 'schema.sql.gen.hh',
  input : 'schema.sql',
  command : ['sh', '-c', sql_header_script]
)

libstore = library(
  'libnixstore',
  [
    'binary-cache-store.cc',
    'build.cc',
    'crypto.cc',
    'derivations.cc',
    'download.cc',
    'export-import.cc',
    'gc.cc',
    'globals.cc',
    'http-binary-cache-store.cc',
    'legacy-ssh-store.cc',
    'local-binary-cache-store.cc',
    'local-fs-store.cc',
    'local-store.cc',
    'machines.cc',
    'misc.cc',
    'nar-accessor.cc',
    'nar-info.cc',
    'nar-info-disk-cache.cc',
    'optimise-store.cc',
    'parsed-derivations.cc',
    'pathlocks.cc',
    'profiles.cc',
    'references.cc',
    'remote-fs-accessor.cc',
    'remote-store.cc',
    's3-binary-cache-store.cc',
    'sqlite.cc',
    'ssh.cc',
    'ssh-store.cc',
    'store-api.cc',

    'builtins/buildenv.cc',
    'builtins/fetchurl.cc',

    sql_header,
  ],
  install : true,
  include_directories : global_subdirs,
  dependencies : [
    global_libs,

    sqlite3,
    bz2,
    libcurl,
    libsodium,
    pthread,

    aws_cpp_sdk_transfer,
    aws_cpp_sdk_s3,
    aws_cpp_sdk_core,
    seccomp,
  ],
  link_with : [
    libutil,
  ],
  cpp_args : [
    '-DNIX_PREFIX=@0@'.format(get_option('prefix')),
    '-DNIX_STORE_DIR=@0@'.format(storedir),
    '-DNIX_DATA_DIR=@0@'.format(get_option('datadir')),
    '-DNIX_STATE_DIR=@0@/nix'.format(get_option('localstatedir')),
    '-DNIX_LOG_DIR=@0@/log/nix'.format(get_option('localstatedir')),
    '-DNIX_CONF_DIR=@0@/nix'.format(get_option('sysconfdir')),
    '-DNIX_LIBEXEC_DIR=@0@'.format(get_option('libexecdir')),
    '-DNIX_BIN_DIR=@0@'.format(get_option('bindir')),
    '-DNIX_MAN_DIR=@0@'.format(get_option('mandir')),
    '-DSANDBOX_SHELL=@0@'.format(sandbox_shell),
    '-DLSOF=@0@'.format(lsof),
  ],
  link_args : host_machine.system() == 'freebsd' ? ['-ldl'] : []
)

libraries += [ libstore ]
