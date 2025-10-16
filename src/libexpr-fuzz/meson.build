project(
  'nix-expr-fuzz',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    'warning_level=1',
    'errorlogs=true',
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

# Dependencies - similar to libexpr-tests but without test frameworks
deps_private_maybe_subproject = [
  dependency('nix-util'),
  dependency('nix-store'),
  dependency('nix-fetchers'),
  dependency('nix-expr'),
]
deps_public_maybe_subproject = []
subdir('nix-meson-build-support/subprojects')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

configdata = configuration_data()
configdata.set_quoted('PACKAGE_VERSION', meson.project_version())

config_h = configure_file(
  configuration : configdata,
  output : 'expr-fuzz-config.hh',
)

subdir('nix-meson-build-support/common')
subdir('nix-meson-build-support/asan-options')

sources = files(
  'fuzz-eval.cc',
)

include_dirs = [ include_directories('.') ]

# Build the fuzzer executable
fuzzer_exe = executable(
  'nix-expr-fuzzer',
  sources,
  config_h,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  install : true,
)

# Install corpus for convenience
install_subdir(
  'corpus',
  install_dir : get_option('datadir') / 'nix-expr-fuzz',
  strip_directory : false,
)
