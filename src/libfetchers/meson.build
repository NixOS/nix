project('nix-fetchers', 'cpp',
  version : run_command('cat', './.version', check : true).stdout().strip(),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 0.64.0',
)

cxx = meson.get_compiler('cpp')

deps_private = [ ]
deps_public = [ ]
deps_other = [ ]

configdata = configuration_data()

nix_store = dependency('nix-store', required : false)
if nix_store.type_name() == 'internal'
  nix_store = subproject('libstore').get_variable('nix_store')
  # subproject sadly no good for pkg-config module
  deps_other += nix_store
else
  deps_public += nix_store
endif

nix_util = dependency('nix-util', required : false)
if nix_util.type_name() == 'internal'
  nix_util = subproject('libutil').get_variable('nix_util')
  # subproject sadly no good for pkg-config module
  deps_other += nix_util
else
  deps_public += nix_util
endif

nlohmann_json = dependency(
  'nlohmann_json',
  version : '>= 3.9',
  required : true,
)

libgit2 = dependency(
  'libgit2',
  required : true,
)

deps_public += libgit2

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  # '-include', 'config-fetchers.h',
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
  'attrs.cc',
  'cache.cc',
  'fetch-settings.cc',
  'fetch-to-store.cc',
  'fetchers.cc',
  'filtering-source-accessor.cc',
  'git.cc',
  'git-utils.cc',
  'github.cc',
  'indirect.cc',
  'mercurial.cc',
  'mounted-source-accessor.cc',
  'path.cc',
  'store-path-accessor.cc',
  'registry.cc',
  'tarball.cc',
)

headers = files(
  'attrs.hh',
  'cache.hh',
  'fetch-settings.hh',
  'fetch-to-store.hh',
  'filtering-source-accessor.hh',
  'git-utils.hh',
  'mounted-source-accessor.hh',
  'fetchers.hh',
  'registry.hh',
  'store-path-accessor.hh',
  'tarball.hh',
)

libfetchers = library(
  'nixfetchers',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

import('pkgconfig').generate(
  libfetchers,
  filebase : 'nix-fetchers',
  name : 'Nix',
  description : 'Nix Package Manager',
  subdirs : ['nix'],
  extra_cflags : ['-std=c++2a'],
  requires : deps_public,
  requires_private : deps_private,
)

include_dirs = [
  include_directories('.'),
]

nix_fetchers = declare_dependency(
  include_directories : include_dirs,
  link_with : libfetchers,
  compile_args : ['-std=c++2a'],
  dependencies : [],
)
meson.override_dependency('nix-fetchers', nix_fetchers)
