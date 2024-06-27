project('nix-util', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('meson-utils/deps-lists')

configdata = configuration_data()

# Check for each of these functions, and create a define like `#define
# HAVE_LUTIMES 1`. The `#define` is unconditional, 0 for not found and 1
# for found. One therefore uses it with `#if` not `#ifdef`.
check_funcs = [
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
]
foreach funcspec : check_funcs
  define_name = 'HAVE_' + funcspec.underscorify().to_upper()
  define_value = cxx.has_function(funcspec).to_int()
  configdata.set(define_name, define_value)
endforeach

# This is only conditional to work around
# https://github.com/mesonbuild/meson/issues/13293. It should be
# unconditional.
if not (host_machine.system() == 'windows' and cxx.get_id() == 'gcc')
  deps_private += dependency('threads')
endif

if host_machine.system() == 'windows'
  socket = cxx.find_library('ws2_32')
  deps_other += socket
elif host_machine.system() == 'sunos'
  socket = cxx.find_library('socket')
  network_service_library = cxx.find_library('nsl')
  deps_other += [socket, network_service_library]
endif

boost = dependency(
  'boost',
  modules : ['context', 'coroutine'],
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

config_h = configure_file(
  configuration : configdata,
  output : 'config-util.hh',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
  '-Werror=unused-result',
  '-Wdeprecated-copy',
  '-Wignored-qualifiers',
  # Enable assertions in libstdc++ by default. Harmless on libc++. Benchmarked
  # at ~1% overhead in `nix search`.
  #
  # FIXME: remove when we get meson 1.4.0 which will default this to on for us:
  # https://mesonbuild.com/Release-notes-for-1-4-0.html#ndebug-setting-now-controls-c-stdlib-assertions
  '-D_GLIBCXX_ASSERTIONS=1',
  language : 'cpp',
)

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

headers = [config_h] + files(
  'abstract-setting-to-json.hh',
  'ansicolor.hh',
  'archive.hh',
  'args.hh',
  'args/root.hh',
  'callback.hh',
  'canon-path.hh',
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

if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  # Windows DLLs are stricter about symbol visibility than Unix shared
  # objects --- see https://gcc.gnu.org/wiki/Visibility for details.
  # This is a temporary sledgehammer to export everything like on Unix,
  # and not detail with this yet.
  #
  # TODO do not do this, and instead do fine-grained export annotations.
  linker_export_flags = ['-Wl,--export-all-symbols']
else
  linker_export_flags = []
endif

this_library = library(
  'nixutil',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

# Part of how we copy boost libraries to a separate installation to
# reduce closure size. These libraries will be copied to our `$out/bin`,
# and these `-l` flags will pick them up there.
#
# https://github.com/NixOS/nixpkgs/issues/45462
libraries_private = ['-lboost_context', '-lboost_coroutine']
if host_machine.system() == 'windows'
  # `libraries_private` cannot contain ad-hoc dependencies (from
  # `find_library), so we need to do this manually
  libraries_private += ['-lws2_32']
endif

subdir('meson-utils/export')
