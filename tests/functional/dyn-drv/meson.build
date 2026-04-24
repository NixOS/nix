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
    'build-built-drv.sh',
    'eval-outputOf.sh',
    'failing-outer.sh',
    'old-daemon-error-hack.sh',
  ],
  'workdir' : meson.current_source_dir(),
}
