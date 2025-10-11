cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/common/asan-options')

libstoreconsumer_tester = executable(
  'test-libstoreconsumer',
  'main.cc',
  dependencies : [
    dependency('nix-store'),
  ],
  build_by_default : false,
)
