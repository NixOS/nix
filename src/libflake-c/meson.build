project('nix-flake-c', 'cpp',
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
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-expr'),
  dependency('nix-flake'),
]
deps_public_maybe_subproject = [
  dependency('nix-util-c'),
  dependency('nix-store-c'),
  dependency('nix-expr-c'),
]
subdir('nix-meson-build-support/subprojects')

# TODO rename, because it will conflict with downstream projects
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

config_h = configure_file(
  configuration : configdata,
  output : 'config-flake.h',
)

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.

  # From C++ libraries, only for internals
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',
  # not generated (yet?)
  # '-include', 'config-flake.hh',

  # From C libraries, for our public, installed headers too
  '-include', 'config-util.h',
  '-include', 'config-store.h',
  '-include', 'config-expr.h',
  '-include', 'config-flake.h',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'nix_api_flake.cc',
)

include_dirs = [include_directories('.')]

headers = [config_h] + files(
  'nix_api_flake.h',
)

# TODO move this header to libexpr, maybe don't use it in tests?
headers += files('nix_api_flake.h')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixflakec',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args: linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
