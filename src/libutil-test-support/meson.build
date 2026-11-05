project(
  'nix-util-test-support',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.8',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

deps_private_maybe_subproject = []
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-util-c'),
]
subdir('nix-meson-build-support/subprojects')

rapidcheck = dependency('rapidcheck')
deps_public += rapidcheck
# rapidcheck's pkg-config file has an empty `Libs:`, so find the library
# ourselves. Unlike a bare `-lrapidcheck`, that gives Meson an absolute
# path, from which it can also find the DLL when running the tests under
# Wine.
#
# TODO: Just use `rapidcheck` once
# https://github.com/emil-e/rapidcheck/pull/326 is available.
deps_private_no_pkgconfig += cxx.find_library(
  'rapidcheck',
  dirs : [ rapidcheck.get_variable('libdir') ],
)

subdir('nix-meson-build-support/common')

sources = files(
  'hash.cc',
  'string_callback.cc',
)

subdir('include/nix/util/tests')

if host_machine.system() == 'windows'
  subdir('windows')
endif

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nix-util-test-support',
  sources,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_public_no_pkgconfig + deps_private_no_pkgconfig,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix/util/tests', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
