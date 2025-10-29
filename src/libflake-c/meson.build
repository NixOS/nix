project(
  'nix-flake-c',
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

deps_private_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
  dependency('nix-expr'),
  dependency('nix-flake'),
]
deps_public_maybe_subproject = [
  dependency('nix-util-c'),
  dependency('nix-store-c'),
  dependency('nix-fetchers-c'),
  dependency('nix-expr-c'),
]
subdir('nix-meson-build-support/subprojects')

subdir('nix-meson-build-support/common')

sources = files(
  'nix_api_flake.cc',
)

include_dirs = [ include_directories('.') ]

headers = files(
  'nix_api_flake.h',
  'nix_api_flake_internal.hh',
)

# TODO move this header to libexpr, maybe don't use it in tests?
headers += files('nix_api_flake.h')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixflakec',
  sources,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
