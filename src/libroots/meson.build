project(
  'nix-roots',
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
deps_public_maybe_subproject = [ dependency('nix-util') ]
subdir('nix-meson-build-support/subprojects')

subdir('nix-meson-build-support/common')

boost = dependency(
  'boost',
  modules : [
    'container',
  ],
  include_type : 'system',
)
deps_other += boost

configdata_priv = configuration_data()

lsof = find_program('lsof', required : false)
configdata_priv.set_quoted(
  'LSOF',
  lsof.found() ? lsof.full_path()
  # Just look up on the PATH
: 'lsof',
)

config_priv_h = configure_file(
  configuration : configdata_priv,
  output : 'roots-config-private.hh',
)

sources = files(
  'roots.cc',
)

subdir('include/nix/roots')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

this_library = library(
  'nixroots',
  sources,
  config_priv_h,
  soversion : nix_soversion,
  dependencies : deps_public + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  install : true,
)

install_headers(headers, subdir : 'nix/roots', preserve_path : true)

libraries_private = []

subdir('nix-meson-build-support/export')
