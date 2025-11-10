project(
  'nix-flake',
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

deps_private_maybe_subproject = []
deps_public_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
  dependency('nix-expr'),
]
subdir('nix-meson-build-support/subprojects')

nlohmann_json = dependency('nlohmann_json', version : '>= 3.9')
deps_public += nlohmann_json

subdir('nix-meson-build-support/common')

subdir('nix-meson-build-support/generate-header')

generated_headers = []
foreach header : [
  'call-flake.nix',
]
  generated_headers += gen_header.process(header)
endforeach

sources = files(
  'config.cc',
  'flake-primops.cc',
  'flake.cc',
  'flakeref.cc',
  'lockfile.cc',
  'settings.cc',
  'url-name.cc',
)

subdir('include/nix/flake')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixflake',
  sources,
  generated_headers,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  prelink : true, # For C++ static initializers
  install : true,
)

install_headers(headers, subdir : 'nix/flake', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
