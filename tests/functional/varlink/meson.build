configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

nix_store = dependency('nix-store', required : false)
if nix_store.found()
  add_languages('cpp')
  subdir('test-varlink')
  suites += {
    'name' : 'varlink',
    'deps' : [ varlink_dynamic, varlink_non_trivial, varlink_trivial ],
    'tests' : [
      'varlink-dynamic.sh',
      'varlink-non-trivial.sh',
      'varlink-trivial.sh',
    ],
    'workdir' : meson.current_source_dir(),
  }

endif

