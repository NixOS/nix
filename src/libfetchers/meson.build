project('nix-fetchers', 'cpp',
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

# See note in ../nix-util/meson.build
deps_private = [ ]

# See note in ../nix-util/meson.build
deps_public = [ ]

# See note in ../nix-util/meson.build
deps_other = [ ]

configdata = configuration_data()

nix_util = dependency('nix-util')
if nix_util.type_name() == 'internal'
  # subproject sadly no good for pkg-config module
  deps_other += nix_util
else
  deps_public += nix_util
endif

nix_store = dependency('nix-store')
if nix_store.type_name() == 'internal'
  # subproject sadly no good for pkg-config module
  deps_other += nix_store
else
  deps_public += nix_store
endif


nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

libgit2 = dependency('libgit2')
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

this_library = library(
  'nixfetchers',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

requires = []
if nix_util.type_name() == 'internal'
  # `requires` cannot contain declared dependencies (from the
  # subproject), so we need to do this manually
  requires += 'nix-util'
endif
if nix_store.type_name() == 'internal'
  requires += 'nix-store'
endif
requires += deps_public

import('pkgconfig').generate(
  this_library,
  filebase : meson.project_name(),
  name : 'Nix',
  description : 'Nix Package Manager',
  subdirs : ['nix'],
  extra_cflags : ['-std=c++2a'],
  requires : requires,
  requires_private : deps_private,
)

meson.override_dependency(meson.project_name(), declare_dependency(
  include_directories : include_directories('.'),
  link_with : this_library,
  compile_args : ['-std=c++2a'],
  dependencies : [nix_util, nix_store],
))
