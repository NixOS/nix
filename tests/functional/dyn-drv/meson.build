configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

suites += {
  'name' : 'dyn-drv',
  'deps' : [],
  'tests' : [
    'text-hashed-output.sh',
    'recursive-mod-json.sh',
    'build-built-drv.sh',
    'eval-outputOf.sh',
    'failing-outer.sh',
    'dep-built-drv.sh',
    'old-daemon-error-hack.sh',
    'dep-built-drv-2.sh',
  ],
  'workdir' : meson.current_source_dir(),
}
