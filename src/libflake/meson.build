project('nix-flake', 'cpp',
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

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
  dependency('nix-expr'),
]
subdir('nix-meson-build-support/subprojects')

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  # '-include', 'config-fetchers.h',
  '-include', 'config-expr.hh',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'flake/config.cc',
  'flake/flake.cc',
  'flake/flakeref.cc',
  'flake/lockfile.cc',
  'flake/settings.cc',
  'flake/url-name.cc',
)

include_dirs = [include_directories('.')]

headers = files(
  'flake/flake.hh',
  'flake/flakeref.hh',
  'flake/lockfile.hh',
  'flake/settings.hh',
  'flake/url-name.hh',
)

this_library = library(
  'nixflake',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
