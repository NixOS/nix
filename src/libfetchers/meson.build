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

subdir('build-utils-meson/deps-lists')

configdata = configuration_data()

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
]
subdir('build-utils-meson/subprojects')

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

libgit2 = dependency('libgit2')
deps_public += libgit2

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  # '-include', 'config-fetchers.h',
  language : 'cpp',
)

subdir('build-utils-meson/diagnostics')

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

include_dirs = [include_directories('.')]

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

libraries_private = []

subdir('build-utils-meson/export')
