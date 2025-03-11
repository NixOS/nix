project('nix-fetchers', 'cpp',
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

configuration_data()

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
]
subdir('nix-meson-build-support/subprojects')

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

libgit2 = dependency('libgit2')
deps_private += libgit2

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  # '-include', 'config-fetchers.h',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'attrs.cc',
  'cache.cc',
  'fetch-settings.cc',
  'fetch-to-store.cc',
  'fetchers.cc',
  'filtering-source-accessor.cc',
  'git-lfs-fetch.cc',
  'git-utils.cc',
  'git.cc',
  'github.cc',
  'indirect.cc',
  'mercurial.cc',
  'path.cc',
  'registry.cc',
  'store-path-accessor.cc',
  'tarball.cc',
)

include_dirs = [include_directories('.')]

headers = files(
  'attrs.hh',
  'cache.hh',
  'fetch-settings.hh',
  'fetch-to-store.hh',
  'fetchers.hh',
  'filtering-source-accessor.hh',
  'git-lfs-fetch.hh',
  'git-utils.hh',
  'registry.hh',
  'store-path-accessor.hh',
  'tarball.hh',
)

this_library = library(
  'nixfetchers',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
