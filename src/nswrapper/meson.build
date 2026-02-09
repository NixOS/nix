project(
  'nix-nswrapper',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
    'localstatedir=/nix/var',
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')

deps_private_maybe_subproject = [
  dependency('nix-util'),
]
deps_public_maybe_subproject = []
subdir('nix-meson-build-support/subprojects')

subdir('nix-meson-build-support/export-all-symbols')
subdir('nix-meson-build-support/windows-version')

subdir('nix-meson-build-support/common')

sources = files('nswrapper.cc')

include_dirs = [ include_directories('.') ]

this_exe = executable(
  meson.project_name(),
  sources,
  dependencies : deps_private_subproject + deps_private + deps_other,
  include_directories : include_dirs,
  link_args : linker_export_flags,
  install : true,
  install_dir : get_option('libexecdir'),
  cpp_pch : do_pch ? [ 'pch/precompiled-headers.hh' ] : [],
)

meson.override_find_program('nix-nswrapper', this_exe)
