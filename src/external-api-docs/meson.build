project(
  'nix-external-api-docs',
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
    'src' : fs.parent(fs.parent(meson.project_source_root())),
  },
)

doxygen = find_program('doxygen', native : true, required : true)

custom_target(
  'external-api-docs',
  command : [ doxygen, doxygen_cfg ],
  input : [
    doxygen_cfg,
  ],
  output : 'html',
  install : true,
  install_dir : get_option('datadir') / 'doc/nix/external-api',
  build_always_stale : true,
)
