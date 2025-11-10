project(
  'nix-util',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

configdata = configuration_data()

deps_private_maybe_subproject = []
deps_public_maybe_subproject = []
subdir('nix-meson-build-support/subprojects')

# Check for each of these functions, and create a define like `#define
# HAVE_POSIX_FALLOCATE 1`. The `#define` is unconditional, 0 for not
# found and 1 for found. One therefore uses it with `#if` not `#ifdef`.
check_funcs = [
  [
    'posix_fallocate',
    'Optionally used to preallocate files to be large enough before writing to them.',
  ],
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec[0].underscorify().to_upper()
  define_value = cxx.has_function(funcspec[0]).to_int()
  configdata.set(define_name, define_value, description : funcspec[1])
endforeach

subdir('nix-meson-build-support/libatomic')

if host_machine.system() == 'windows'
  socket = cxx.find_library('ws2_32')
  deps_other += socket
elif host_machine.system() == 'sunos'
  socket = cxx.find_library('socket')
  network_service_library = cxx.find_library('nsl')
  deps_other += [ socket, network_service_library ]
endif

blake3 = dependency(
  'libblake3',
  version : '>= 1.8.2',
  method : 'pkg-config',
)
deps_private += blake3

boost = dependency(
  'boost',
  modules : [
    'context',
    'coroutine',
    'iostreams',
    'url',
  ],
  include_type : 'system',
  version : '>=1.87.0',
)
# boost is a public dependency, but not a pkg-config dependency unfortunately, so we
# put in `deps_other`.
deps_other += boost

openssl = dependency(
  'libcrypto',
  'openssl',
  version : '>= 1.1.1',
)
deps_private += openssl

libarchive = dependency('libarchive', version : '>= 3.1.2')
deps_public += libarchive
if get_option('default_library') == 'static'
  # Workaround until https://github.com/libarchive/libarchive/issues/1446 is fixed
  add_project_arguments('-lz', language : 'cpp')
endif

sodium = dependency('libsodium', 'sodium')
deps_private += sodium

brotli = [
  dependency('libbrotlicommon'),
  dependency('libbrotlidec'),
  dependency('libbrotlienc'),
]
deps_private += brotli

cpuid_required = get_option('cpuid')
if host_machine.cpu_family() != 'x86_64' and cpuid_required.enabled()
  warning('Force-enabling seccomp on non-x86_64 does not make sense')
endif
cpuid = dependency(
  'libcpuid',
  'cpuid',
  version : '>= 0.7.0',
  required : cpuid_required,
)
configdata.set('HAVE_LIBCPUID', cpuid.found().to_int())
deps_private += cpuid

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

cxx = meson.get_compiler('cpp')

config_priv_h = configure_file(
  configuration : configdata,
  output : 'util-config-private.hh',
)

subdir('nix-meson-build-support/common')

sources = [ config_priv_h ] + files(
  'archive.cc',
  'args.cc',
  'base-n.cc',
  'base-nix-32.cc',
  'canon-path.cc',
  'compression.cc',
  'compute-levels.cc',
  'config-global.cc',
  'configuration.cc',
  'current-process.cc',
  'english.cc',
  'environment-variables.cc',
  'error.cc',
  'executable-path.cc',
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
  'mounted-source-accessor.cc',
  'pos-table.cc',
  'position.cc',
  'posix-source-accessor.cc',
  'serialise.cc',
  'signature/local-keys.cc',
  'signature/signer.cc',
  'source-accessor.cc',
  'source-path.cc',
  'strings.cc',
  'suggestions.cc',
  'tarfile.cc',
  'tee-logger.cc',
  'terminal.cc',
  'thread-pool.cc',
  'union-source-accessor.cc',
  'unix-domain-socket.cc',
  'url.cc',
  'users.cc',
  'util.cc',
  'xml-writer.cc',
)

subdir('include/nix/util')

if not cxx.has_header('widechar_width.h', required : false)
  # use vendored widechar_width.h
  include_dirs += include_directories('./widecharwidth')
endif

if host_machine.system() == 'linux'
  subdir('linux')
endif

if host_machine.system() == 'freebsd'
  subdir('freebsd')
endif

if host_machine.system() == 'windows'
  subdir('windows')
else
  subdir('unix')
endif

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixutil',
  sources,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
)

install_headers(headers, subdir : 'nix/util', preserve_path : true)

libraries_private = []
if host_machine.system() == 'windows'
  # `libraries_private` cannot contain ad-hoc dependencies (from
  # `find_library), so we need to do this manually
  libraries_private += [ '-lws2_32' ]
endif

subdir('nix-meson-build-support/export')
