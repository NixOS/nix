project('nix-util-test-support', 'cpp',
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
  dependency('nix-util-c'),
]
subdir('nix-meson-build-support/subprojects')

rapidcheck = dependency('rapidcheck')
deps_public += rapidcheck

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  language : 'cpp',
)

subdir('nix-meson-build-support/common')

sources = files(
  'tests/hash.cc',
  'tests/string_callback.cc',
)

include_dirs = [include_directories('.')]

headers = files(
  'tests/characterization.hh',
  'tests/gtest-with-params.hh',
  'tests/hash.hh',
  'tests/nix_api_util.hh',
  'tests/string_callback.hh',
)

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nix-util-test-support',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  # TODO: Remove `-lrapidcheck` when https://github.com/emil-e/rapidcheck/pull/326
  #       is available. See also ../libutil/build.meson
  link_args: linker_export_flags + ['-lrapidcheck'],
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
