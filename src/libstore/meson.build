project('nix-store', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
    'localstatedir=/nix/var',
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

configdata = configuration_data()

# TODO rename, because it will conflict with downstream projects
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

configdata.set_quoted('SYSTEM', host_machine.cpu_family() + '-' + host_machine.system())

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
]
subdir('nix-meson-build-support/subprojects')

run_command('ln', '-s',
  meson.project_build_root() / '__nothing_link_target',
  meson.project_build_root() / '__nothing_symlink',
  # native doesn't allow dangling symlinks, which the tests require
  env : { 'MSYS' : 'winsymlinks:lnk' },
  check : true,
)
can_link_symlink = run_command('ln',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : false,
).returncode() == 0
run_command('rm', '-f',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : true,
)
summary('can hardlink to symlink', can_link_symlink, bool_yn : true)
configdata.set('CAN_LINK_SYMLINK', can_link_symlink.to_int())

# Check for each of these functions, and create a define like `#define HAVE_LCHOWN 1`.
#
# Only need to do functions that deps (like `libnixutil`) didn't already
# check for.
check_funcs = [
  # Optionally used for canonicalising files from the build
  'lchown',
  'statvfs',
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata.set(define_name, define_value)
endforeach

has_acl_support = cxx.has_header('sys/xattr.h') \
  and cxx.has_function('llistxattr') \
  and cxx.has_function('lremovexattr')
configdata.set('HAVE_ACL_SUPPORT', has_acl_support.to_int())

if host_machine.system() == 'darwin'
  sandbox = cxx.find_library('sandbox')
  deps_other += [sandbox]
endif

if host_machine.system() == 'windows'
  wsock32 = cxx.find_library('wsock32')
  deps_other += [wsock32]
endif

subdir('nix-meson-build-support/libatomic')

boost = dependency(
  'boost',
  modules : ['container'],
  include_type: 'system',
)
# boost is a public dependency, but not a pkg-config dependency unfortunately, so we
# put in `deps_other`.
deps_other += boost

curl = dependency('libcurl', 'curl')
deps_private += curl

# seccomp only makes sense on Linux
is_linux = host_machine.system() == 'linux'
seccomp_required = get_option('seccomp-sandboxing')
if not is_linux and seccomp_required.enabled()
  warning('Force-enabling seccomp on non-Linux does not make sense')
endif
seccomp = dependency('libseccomp', 'seccomp', required : seccomp_required, version : '>=2.5.5')
if is_linux and not seccomp.found()
  warning('Sandbox security is reduced because libseccomp has not been found! Please provide libseccomp if it supports your CPU architecture.')
endif
configdata.set('HAVE_SECCOMP', seccomp.found().to_int())
deps_private += seccomp

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

sqlite = dependency('sqlite3', 'sqlite', version : '>=3.6.19')
deps_private += sqlite

# AWS C++ SDK has bad pkg-config
aws_s3 = dependency('aws-cpp-sdk-s3', required : false)
configdata.set('ENABLE_S3', aws_s3.found().to_int())
if aws_s3.found()
  aws_s3 = declare_dependency(
    include_directories: include_directories(aws_s3.get_variable('includedir')),
    link_args: [
      '-L' + aws_s3.get_variable('libdir'),
      '-laws-cpp-sdk-transfer',
      '-laws-cpp-sdk-s3',
      '-laws-cpp-sdk-core',
      '-laws-crt-cpp',
    ],
  ).as_system('system')
endif
deps_other += aws_s3

subdir('nix-meson-build-support/generate-header')

generated_headers = []
foreach header : [
  'schema.sql',
  'ca-specific-schema.sql',
]
  generated_headers += gen_header.process(header)
endforeach

busybox = find_program(get_option('sandbox-shell'), required : false)

if get_option('embedded-sandbox-shell')
  # This one goes in config.h
  # The path to busybox is passed as a -D flag when compiling this_library.
  # This solution is inherited from the old make buildsystem
  # TODO: do this differently?
  configdata.set('HAVE_EMBEDDED_SANDBOX_SHELL', 1)
  hexdump = find_program('hexdump', native : true)
  embedded_sandbox_shell_gen = custom_target(
    'embedded-sandbox-shell.gen.hh',
    command : [
      hexdump,
      '-v',
      '-e',
      '1/1 "0x%x," "\n"'
    ],
    input : busybox.full_path(),
    output : 'embedded-sandbox-shell.gen.hh',
    capture : true,
    feed : true,
  )
  generated_headers += embedded_sandbox_shell_gen
endif

config_h = configure_file(
  configuration : configdata,
  output : 'config-store.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'binary-cache-store.cc',
  'build-result.cc',
  'build/derivation-goal.cc',
  'build/drv-output-substitution-goal.cc',
  'build/entry-points.cc',
  'build/goal.cc',
  'build/substitution-goal.cc',
  'build/worker.cc',
  'builtins/buildenv.cc',
  'builtins/fetchurl.cc',
  'builtins/unpack-channel.cc',
  'common-protocol.cc',
  'common-ssh-store-config.cc',
  'content-address.cc',
  'daemon.cc',
  'derivations.cc',
  'derived-path-map.cc',
  'derived-path.cc',
  'downstream-placeholder.cc',
  'dummy-store.cc',
  'export-import.cc',
  'filetransfer.cc',
  'gc.cc',
  'globals.cc',
  'http-binary-cache-store.cc',
  'indirect-root-store.cc',
  'keys.cc',
  'legacy-ssh-store.cc',
  'local-binary-cache-store.cc',
  'local-fs-store.cc',
  'local-overlay-store.cc',
  'local-store.cc',
  'log-store.cc',
  'machines.cc',
  'make-content-addressed.cc',
  'misc.cc',
  'names.cc',
  'nar-accessor.cc',
  'nar-info-disk-cache.cc',
  'nar-info.cc',
  'optimise-store.cc',
  'outputs-spec.cc',
  'parsed-derivations.cc',
  'path-info.cc',
  'path-references.cc',
  'path-with-outputs.cc',
  'path.cc',
  'pathlocks.cc',
  'posix-fs-canonicalise.cc',
  'profiles.cc',
  'realisation.cc',
  'remote-fs-accessor.cc',
  'remote-store.cc',
  's3-binary-cache-store.cc',
  'serve-protocol-connection.cc',
  'serve-protocol.cc',
  'sqlite.cc',
  'ssh-store.cc',
  'ssh.cc',
  'store-api.cc',
  'store-reference.cc',
  'uds-remote-store.cc',
  'worker-protocol-connection.cc',
  'worker-protocol.cc',
)

include_dirs = [
  include_directories('.'),
  include_directories('build'),
]

headers = [config_h] + files(
  'binary-cache-store.hh',
  'build-result.hh',
  'build/derivation-goal.hh',
  'build/drv-output-substitution-goal.hh',
  'build/goal.hh',
  'build/substitution-goal.hh',
  'build/worker.hh',
  'builtins.hh',
  'builtins/buildenv.hh',
  'common-protocol-impl.hh',
  'common-protocol.hh',
  'common-ssh-store-config.hh',
  'content-address.hh',
  'daemon.hh',
  'derivations.hh',
  'derived-path-map.hh',
  'derived-path.hh',
  'downstream-placeholder.hh',
  'filetransfer.hh',
  'gc-store.hh',
  'globals.hh',
  'http-binary-cache-store.hh',
  'indirect-root-store.hh',
  'keys.hh',
  'legacy-ssh-store.hh',
  'length-prefixed-protocol-helper.hh',
  'local-binary-cache-store.hh',
  'local-fs-store.hh',
  'local-overlay-store.hh',
  'local-store.hh',
  'log-store.hh',
  'machines.hh',
  'make-content-addressed.hh',
  'names.hh',
  'nar-accessor.hh',
  'nar-info-disk-cache.hh',
  'nar-info.hh',
  'outputs-spec.hh',
  'parsed-derivations.hh',
  'path-info.hh',
  'path-references.hh',
  'path-regex.hh',
  'path-with-outputs.hh',
  'path.hh',
  'pathlocks.hh',
  'posix-fs-canonicalise.hh',
  'profiles.hh',
  'realisation.hh',
  'remote-fs-accessor.hh',
  'remote-store-connection.hh',
  'remote-store.hh',
  's3-binary-cache-store.hh',
  's3.hh',
  'ssh-store.hh',
  'serve-protocol-connection.hh',
  'serve-protocol-impl.hh',
  'serve-protocol.hh',
  'sqlite.hh',
  'ssh.hh',
  'store-api.hh',
  'store-cast.hh',
  'store-dir-config.hh',
  'store-reference.hh',
  'uds-remote-store.hh',
  'worker-protocol-connection.hh',
  'worker-protocol-impl.hh',
  'worker-protocol.hh',
)

if host_machine.system() == 'linux'
  subdir('linux')
endif

if host_machine.system() == 'windows'
  subdir('windows')
else
  subdir('unix')
endif

fs = import('fs')

prefix = get_option('prefix')
# For each of these paths, assume that it is relative to the prefix unless
# it is already an absolute path (which is the default for store-dir, localstatedir, and log-dir).
path_opts = [
  # Meson built-ins.
  'datadir',
  'mandir',
  'libdir',
  'includedir',
  'libexecdir',
  # Homecooked Nix directories.
  'store-dir',
  'localstatedir',
  'log-dir',
]
# For your grepping pleasure, this loop sets the following variables that aren't mentioned
# literally above:
# store_dir
# localstatedir
# log_dir
# profile_dir
foreach optname : path_opts
  varname = optname.replace('-', '_')
  path = get_option(optname)
  if fs.is_absolute(path)
    set_variable(varname, path)
  else
    set_variable(varname, prefix / path)
  endif
endforeach

# sysconfdir doesn't get anything installed to directly, and is only used to
# tell Nix where to look for nix.conf, so it doesn't get appended to prefix.
sysconfdir = get_option('sysconfdir')
if not fs.is_absolute(sysconfdir)
  sysconfdir = '/' / sysconfdir
endif

lsof = find_program('lsof', required : false)

# Aside from prefix itself, each of these was made into an absolute path
# by joining it with prefix, unless it was already an absolute path
# (which is the default for store-dir, localstatedir, and log-dir).
cpp_str_defines = {
  'NIX_PREFIX':    prefix,
  'NIX_STORE_DIR': store_dir,
  'NIX_DATA_DIR':  datadir,
  'NIX_STATE_DIR': localstatedir / 'nix',
  'NIX_LOG_DIR':   log_dir,
  'NIX_CONF_DIR':  sysconfdir / 'nix',
  'NIX_MAN_DIR':   mandir,
}

if lsof.found()
  lsof_path = lsof.full_path()
else
  # Just look up on the PATH
  lsof_path = 'lsof'
endif
cpp_str_defines += {
  'LSOF': lsof_path
}

if get_option('embedded-sandbox-shell')
  cpp_str_defines += {
    'SANDBOX_SHELL': '__embedded_sandbox_shell__'
  }
elif busybox.found()
  cpp_str_defines += {
    'SANDBOX_SHELL': busybox.full_path()
  }
endif

cpp_args = []

foreach name, value : cpp_str_defines
  cpp_args += [
    '-D' + name + '=' + '"' + value + '"'
  ]
endforeach

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixstore',
  generated_headers,
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  cpp_args : cpp_args,
  link_args: linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

extra_pkg_config_variables = {
  'storedir' : get_option('store-dir'),
}

# Working around https://github.com/mesonbuild/meson/issues/13584
if host_machine.system() != 'darwin'
  extra_pkg_config_variables += {
    'localstatedir' : get_option('localstatedir'),
  }
endif

subdir('nix-meson-build-support/export')
