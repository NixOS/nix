project(
  'nix-internal-api-docs',
  version : files('.version'),
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

fs = import('fs')

doxygen_cfg = configure_file(
  input : 'doxygen.cfg.in',
  output : 'doxygen.cfg',
  configuration : {
    'PROJECT_NUMBER' : meson.project_version(),
    'OUTPUT_DIRECTORY' : meson.current_build_dir(),
    'BUILD_ROOT' : meson.build_root(),
    'src' : fs.parent(fs.parent(meson.project_source_root())) / 'src',
  },
)

doxygen = find_program('doxygen', native : true, required : true)

custom_target(
  'internal-api-docs',
  command : [ doxygen, doxygen_cfg ],
  input : [
    doxygen_cfg,
  ],
  output : 'html',
  install : true,
  install_dir : get_option('datadir') / 'doc/nix/internal-api',
  build_always_stale : true,
)
