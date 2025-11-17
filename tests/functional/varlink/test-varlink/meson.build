cxx = meson.get_compiler('cpp')

varlink_trivial = executable(
  'varlink-trivial',
  'trivial.cc',
  dependencies : deps_other + [
    dependency('nix-store'),
  ],
  build_by_default : false,
)

varlink_dynamic = executable(
  'varlink-dynamic',
  'dynamic.cc',
  dependencies : deps_other + [
    dependency('nix-store'),
  ],
  build_by_default : false,
)

varlink_non_trivial = executable(
  'varlink-non-trivial',
  'non-trivial.cc',
  dependencies : deps_other + [
    dependency('nix-store'),
  ],
  build_by_default : false,
)
