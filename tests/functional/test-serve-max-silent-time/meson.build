cxx = meson.get_compiler('cpp')

serve_max_silent_time_tester = executable(
  'test-serve-max-silent-time',
  'main.cc',
  dependencies : [
    dependency('nix-store'),
  ],
  build_by_default : false,
)
