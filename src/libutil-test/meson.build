project('nix-util-test', 'cpp',
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
  dependency('nix-util'),
  dependency('nix-util-c'),
  dependency('nix-util-test-support'),
]
deps_public_maybe_subproject = [
]
subdir('build-utils-meson/subprojects')

subdir('build-utils-meson/export-all-symbols')

rapidcheck = dependency('rapidcheck')
deps_private += rapidcheck

gtest = dependency('gtest', main : true)
deps_private += gtest

add_project_arguments(
  # TODO(Qyriad): Yes this is how the autoconf+Make system did it.
  # It would be nice for our headers to be idempotent instead.
  '-include', 'config-util.hh',
  '-include', 'config-util.h',
  language : 'cpp',
)

subdir('build-utils-meson/diagnostics')

sources = files(
  'args.cc',
  'canon-path.cc',
  'chunked-vector.cc',
  'closure.cc',
  'compression.cc',
  'config.cc',
  'file-content-address.cc',
  'git.cc',
  'hash.cc',
  'hilite.cc',
  'json-utils.cc',
  'logging.cc',
  'lru-cache.cc',
  'nix_api_util.cc',
  'pool.cc',
  'references.cc',
  'spawn.cc',
  'suggestions.cc',
  'tests.cc',
  'url.cc',
  'xml-writer.cc',
)

include_dirs = [include_directories('.')]


this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  # TODO: -lrapidcheck, see ../libutil-support/build.meson
  link_args: linker_export_flags + ['-lrapidcheck'],
  # get main from gtest
  install : true,
)

test(meson.project_name(), this_exe, env : ['_NIX_TEST_UNIT_DATA=' + meson.current_source_dir() + '/data'])
