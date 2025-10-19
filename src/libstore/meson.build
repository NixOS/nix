project(
  'nix-store',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
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

configdata_pub = configuration_data()
configdata_priv = configuration_data()

# TODO rename, because it will conflict with downstream projects
configdata_priv.set_quoted('PACKAGE_VERSION', meson.project_version())

subdir('nix-meson-build-support/default-system-cpu')

# Used in public header.
configdata_pub.set_quoted(
  'NIX_LOCAL_SYSTEM',
  nix_system_cpu + '-' + host_machine.system(),
  description : 'This is the system name Nix expects for local running instance of Nix.\n\n'
    + 'See the "system" setting for additional details',
)

deps_private_maybe_subproject = []
deps_public_maybe_subproject = [
  dependency('nix-util'),
]
subdir('nix-meson-build-support/subprojects')

run_command(
  'ln',
  '-s',
  meson.project_build_root() / '__nothing_link_target',
  meson.project_build_root() / '__nothing_symlink',
  # native doesn't allow dangling symlinks, which the tests require
  env : {'MSYS' : 'winsymlinks:lnk'},
  check : true,
)
can_link_symlink = run_command(
  'ln',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : false,
).returncode() == 0
run_command(
  'rm',
  '-f',
  meson.project_build_root() / '__nothing_symlink',
  meson.project_build_root() / '__nothing_hardlink',
  check : true,
)
summary('can hardlink to symlink', can_link_symlink, bool_yn : true)
configdata_priv.set('CAN_LINK_SYMLINK', can_link_symlink.to_int())

# Check for each of these functions, and create a define like `#define HAVE_LCHOWN 1`.
check_funcs = [
  # Optionally used for canonicalising files from the build
  'lchown',
  'posix_fallocate',
  'statvfs',
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata_priv.set(define_name, define_value)
endforeach

has_acl_support = cxx.has_header('sys/xattr.h') \
  and cxx.has_function('llistxattr') \
  and cxx.has_function('lremovexattr')
# Used in public header. Affects ABI!
configdata_pub.set(
  'NIX_SUPPORT_ACL',
  has_acl_support.to_int(),
  description : 'FIXME: It\'s a bit peculiar that this needs to be exposed. The reason is that that it effects whether the settings struct in a header has a particular field. This is also odd, because it means when there is no ACL support one will just get an "unknown setting" warning from their configuration.',
)

if host_machine.system() == 'darwin'
  sandbox = cxx.find_library('sandbox')
  deps_other += [ sandbox ]
endif

if host_machine.system() == 'windows'
  wsock32 = cxx.find_library('wsock32')
  deps_other += [ wsock32 ]
endif

subdir('nix-meson-build-support/libatomic')

boost = dependency(
  'boost',
  modules : [
    'container',
    # Shouldn't list, because can header-only, and Meson currently looks for libs
    #'regex',
    'url',
  ],
  include_type : 'system',
)
# boost is a public dependency, but not a pkg-config dependency unfortunately, so we
# put in `deps_other`.
deps_other += boost

curl = dependency('libcurl', 'curl', version : '>= 7.75.0')
deps_private += curl

# seccomp only makes sense on Linux
is_linux = host_machine.system() == 'linux'
seccomp_required = get_option('seccomp-sandboxing')
if not is_linux and seccomp_required.enabled()
  warning('Force-enabling seccomp on non-Linux does not make sense')
endif
seccomp = dependency(
  'libseccomp',
  'seccomp',
  required : seccomp_required,
  version : '>=2.5.5',
)
if is_linux and not seccomp.found()
  warning(
    'Sandbox security is reduced because libseccomp has not been found! Please provide libseccomp if it supports your CPU architecture.',
  )
endif
configdata_priv.set('HAVE_SECCOMP', seccomp.found().to_int())
deps_private += seccomp

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

sqlite = dependency('sqlite3', 'sqlite', version : '>=3.6.19')
deps_private += sqlite

s3_aws_auth = get_option('s3-aws-auth')
aws_crt_cpp = cxx.find_library('aws-crt-cpp', required : s3_aws_auth)

if s3_aws_auth.enabled()
  deps_other += aws_crt_cpp
  aws_c_common = cxx.find_library('aws-c-common', required : true)
  deps_other += aws_c_common
endif

configdata_pub.set('NIX_WITH_AWS_AUTH', s3_aws_auth.enabled().to_int())

subdir('nix-meson-build-support/generate-header')

generated_headers = []
foreach header : [
  'schema.sql',
  'ca-specific-schema.sql',
]
  generated_headers += gen_header.process(header)
endforeach

busybox = find_program(get_option('sandbox-shell'), required : false)

configdata_priv.set(
  'HAVE_EMBEDDED_SANDBOX_SHELL',
  get_option('embedded-sandbox-shell').to_int(),
)

if get_option('embedded-sandbox-shell')
  configdata_priv.set_quoted('SANDBOX_SHELL', '__embedded_sandbox_shell__')
elif busybox.found()
  configdata_priv.set_quoted('SANDBOX_SHELL', busybox.full_path())
endif

if get_option('embedded-sandbox-shell')
  hexdump = find_program('hexdump', native : true)
  embedded_sandbox_shell_gen = custom_target(
    'embedded-sandbox-shell.gen.hh',
    command : [ hexdump, '-v', '-e', '1/1 "0x%x," "\n"' ],
    input : busybox.full_path(),
    output : 'embedded-sandbox-shell.gen.hh',
    capture : true,
    feed : true,
  )
  generated_headers += embedded_sandbox_shell_gen
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

# Aside from prefix itself, each of these was made into an absolute path
# by joining it with prefix, unless it was already an absolute path
# (which is the default for store-dir, localstatedir, and log-dir).
configdata_priv.set_quoted('NIX_PREFIX', prefix)
configdata_priv.set_quoted('NIX_STORE_DIR', store_dir)
configdata_priv.set_quoted('NIX_DATA_DIR', datadir)
configdata_priv.set_quoted('NIX_STATE_DIR', localstatedir / 'nix')
configdata_priv.set_quoted('NIX_LOG_DIR', log_dir)
configdata_priv.set_quoted('NIX_CONF_DIR', sysconfdir / 'nix')
configdata_priv.set_quoted('NIX_MAN_DIR', mandir)

lsof = find_program('lsof', required : false)
configdata_priv.set_quoted(
  'LSOF',
  lsof.found() ? lsof.full_path()
  # Just look up on the PATH
: 'lsof',
)

config_priv_h = configure_file(
  configuration : configdata_priv,
  output : 'store-config-private.hh',
)

subdir('nix-meson-build-support/common')

sources = files(
  'binary-cache-store.cc',
  'build-result.cc',
  'build/derivation-builder.cc',
  'build/derivation-building-goal.cc',
  'build/derivation-check.cc',
  'build/derivation-env-desugar.cc',
  'build/derivation-goal.cc',
  'build/derivation-resolution-goal.cc',
  'build/derivation-trampoline-goal.cc',
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
  'derivation-options.cc',
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
  'references.cc',
  'remote-fs-accessor.cc',
  'remote-store.cc',
  'restricted-store.cc',
  's3-binary-cache-store.cc',
  's3-url.cc',
  'serve-protocol-connection.cc',
  'serve-protocol.cc',
  'sqlite.cc',
  'ssh-store.cc',
  'ssh.cc',
  'store-api.cc',
  'store-dir-config.cc',
  'store-reference.cc',
  'store-registration.cc',
  'uds-remote-store.cc',
  'worker-protocol-connection.cc',
  'worker-protocol.cc',
)

# AWS credentials code requires AWS CRT, so only compile when enabled
if s3_aws_auth.enabled()
  sources += files('aws-creds.cc')
endif

subdir('include/nix/store')

if host_machine.system() == 'linux'
  subdir('linux')
endif

if host_machine.system() == 'windows'
  subdir('windows')
else
  subdir('unix')
endif

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixstore',
  generated_headers,
  sources,
  config_priv_h,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
)

install_headers(headers, subdir : 'nix/store', preserve_path : true)

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
