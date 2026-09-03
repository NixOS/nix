suites += {
  'name' : 'repl',
  'deps' : [],
  'tests' : [
    'characterisation.sh',
    'edit-builtin-files.sh',
    'simple.sh',
    'reload-flakes.sh',
  ],
  'workdir' : meson.current_source_dir(),
  'env' : {'BASH_ENV' : meson.current_source_dir() / 'common.sh'},
  'overrides' : {
    'characterisation.sh' : {'protocol' : 'tap'},
  },
}
