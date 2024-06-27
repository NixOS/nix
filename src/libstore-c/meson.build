project('nix-store-c', 'cpp',
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

deps_private_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
]
deps_public_maybe_subproject = [
  dependency('nix-util-c'),
]
subdir('meson-utils/subprojects')

# TODO rename, because it will conflict with downstream projects
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

config_h = configure_file(
  configuration : configdata,
  output : 'config-store.h',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.

  # From C++ libraries, only for internals
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',

  # From C libraries, for our public, installed headers too
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  '-Wno-deprecated-declarations',
  '-Wimplicit-fallthrough',
  '-Werror=switch',
  '-Werror=switch-enum',
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
  'nix_api_store.cc',
)

include_dirs = [include_directories('.')]

headers = [config_h] + files(
  'nix_api_store.h',
)

if host_machine.system() == 'cygwin' or host_machine.system() == 'windows'
  # Windows DLLs are stricter ab_subprojectout symbol visibility than Unix shared
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
  'nixstorec',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('meson-utils/export')
