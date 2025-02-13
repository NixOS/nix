project('nix-util', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
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

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
]
subdir('nix-meson-build-support/subprojects')

# Check for each of these functions, and create a define like `#define
# HAVE_LUTIMES 1`. The `#define` is unconditional, 0 for not found and 1
# for found. One therefore uses it with `#if` not `#ifdef`.
check_funcs = [
  'close_range',
  # Optionally used for changing the mtime of symlinks.
  'lutimes',
  # Optionally used for creating pipes on Unix
  'pipe2',
  # Optionally used to preallocate files to be large enough before
  # writing to them.
  'posix_fallocate',
  # Optionally used to get more information about processes failing due
  # to a signal on Unix.
  'strsignal',
  # Optionally used to try to close more file descriptors (e.g. before
  # forking) on Unix.
  'sysconf',
  # Optionally used for changing the mtime of files and symlinks.
  'utimensat',
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata.set(define_name, define_value)
endforeach

configdata.set('HAVE_DECL_AT_SYMLINK_NOFOLLOW', cxx.has_header_symbol('fcntl.h', 'AT_SYMLINK_NOFOLLOW').to_int())

subdir('nix-meson-build-support/libatomic')

if host_machine.system() == 'windows'
  socket = cxx.find_library('ws2_32')
  deps_other += socket
elif host_machine.system() == 'sunos'
  socket = cxx.find_library('socket')
  network_service_library = cxx.find_library('nsl')
  deps_other += [socket, network_service_library]
endif

blake3 = dependency(
  'libblake3',
  version: '>= 1.5.5',
)
deps_private += blake3

boost = dependency(
  'boost',
  modules : ['context', 'coroutine'],
  include_type: 'system',
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
cpuid = dependency('libcpuid', 'cpuid', required : cpuid_required)
configdata.set('HAVE_LIBCPUID', cpuid.found().to_int())
deps_private += cpuid

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

cxx = meson.get_compiler('cpp')

config_h = configure_file(
  configuration : configdata,
  output : 'config-util.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'archive.cc',
  'args.cc',
  'canon-path.cc',
  'compression.cc',
  'compute-levels.cc',
  'config.cc',
  'config-global.cc',
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
  'position.cc',
  'posix-source-accessor.cc',
  'references.cc',
  'serialise.cc',
  'signature/local-keys.cc',
  'signature/signer.cc',
  'source-accessor.cc',
  'source-path.cc',
  'strings.cc',
  'suggestions.cc',
  'tarfile.cc',
  'terminal.cc',
  'thread-pool.cc',
  'unix-domain-socket.cc',
  'url.cc',
  'users.cc',
  'util.cc',
  'xml-writer.cc',
)

include_dirs = [include_directories('.')]
if not cxx.has_header('widechar_width.h', required : false)
  # use vendored widechar_width.h
  include_dirs += include_directories('./widecharwidth')
endif

headers = [config_h] + files(
  'abstract-setting-to-json.hh',
  'ansicolor.hh',
  'archive.hh',
  'args.hh',
  'args/root.hh',
  'callback.hh',
  'canon-path.hh',
  'checked-arithmetic.hh',
  'chunked-vector.hh',
  'closure.hh',
  'comparator.hh',
  'compression.hh',
  'compute-levels.hh',
  'config-global.hh',
  'config-impl.hh',
  'config.hh',
  'current-process.hh',
  'english.hh',
  'environment-variables.hh',
  'error.hh',
  'exec.hh',
  'executable-path.hh',
  'exit.hh',
  'experimental-features.hh',
  'file-content-address.hh',
  'file-descriptor.hh',
  'file-path-impl.hh',
  'file-path.hh',
  'file-system.hh',
  'finally.hh',
  'fmt.hh',
  'fs-sink.hh',
  'git.hh',
  'hash.hh',
  'hilite.hh',
  'json-impls.hh',
  'json-utils.hh',
  'logging.hh',
  'lru-cache.hh',
  'memory-source-accessor.hh',
  'muxable-pipe.hh',
  'os-string.hh',
  'pool.hh',
  'position.hh',
  'posix-source-accessor.hh',
  'processes.hh',
  'ref.hh',
  'references.hh',
  'regex-combinators.hh',
  'repair-flag.hh',
  'serialise.hh',
  'signals.hh',
  'signature/local-keys.hh',
  'signature/signer.hh',
  'source-accessor.hh',
  'source-path.hh',
  'split.hh',
  'std-hash.hh',
  'strings.hh',
  'strings-inline.hh',
  'suggestions.hh',
  'sync.hh',
  'tarfile.hh',
  'terminal.hh',
  'thread-pool.hh',
  'topo-sort.hh',
  'types.hh',
  'unix-domain-socket.hh',
  'url-parts.hh',
  'url.hh',
  'users.hh',
  'util.hh',
  'variant-wrapper.hh',
  'xml-writer.hh',
)

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
  'nixutil',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []
if host_machine.system() == 'windows'
  # `libraries_private` cannot contain ad-hoc dependencies (from
  # `find_library), so we need to do this manually
  libraries_private += ['-lws2_32']
endif

subdir('nix-meson-build-support/export')
