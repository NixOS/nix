project('nix-expr-test-support', 'cpp',
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
  dependency('nix-util-test-support'),
  dependency('nix-store'),
  dependency('nix-store-test-support'),
  dependency('nix-expr'),
]
subdir('build-utils-meson/subprojects')

rapidcheck = dependency('rapidcheck')
deps_public += rapidcheck

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-store.hh',
  '-include', 'config-expr.hh',
  language : 'cpp',
)

subdir('build-utils-meson/diagnostics')

sources = files(
  'tests/value/context.cc',
)

include_dirs = [include_directories('.')]

headers = files(
  'tests/libexpr.hh',
  'tests/nix_api_expr.hh',
  'tests/value/context.hh',
)

subdir('build-utils-meson/export-all-symbols')

this_library = library(
  'nix-expr-test-support',
  sources,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  # TODO: Remove `-lrapidcheck` when https://github.com/emil-e/rapidcheck/pull/326
  #       is available. See also ../libutil/build.meson
  link_args: linker_export_flags + ['-lrapidcheck'],
  install : true,
)

install_headers(headers, subdir : 'nix', preserve_path : true)

libraries_private = []

subdir('build-utils-meson/export')
