cxx = meson.get_compiler('cpp')

deps_private_no_pkgconfig = []
subdir('nix-meson-build-support/common/asan-options')

libstoreconsumer_tester = executable(
  'test-libstoreconsumer',
  'main.cc',
  dependencies : deps_private_no_pkgconfig + [
    dependency('nix-store'),
  ],
  build_by_default : false,
)
