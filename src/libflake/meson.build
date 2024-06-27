project('nix-flake', 'cpp',
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

deps_private_maybe_subproject = [
]
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
  dependency('nix-expr'),
]
subdir('build-utils-meson/subprojects')

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

subdir('build-utils-meson/diagnostics')

sources = files(
  'flake-settings.cc',
  'flake/config.cc',
  'flake/flake.cc',
  'flake/flakeref.cc',
  'flake/url-name.cc',
  'flake/lockfile.cc',
)

include_dirs = [include_directories('.')]

headers = files(
  'flake-settings.hh',
  'flake/flake.hh',
  'flake/flakeref.hh',
  'flake/lockfile.hh',
  'flake/url-name.hh',
)

this_library = library(
  'nixflake',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('build-utils-meson/export')
