project(
  'nix-clang-tidy-plugin',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    'warning_level=2',
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

subdir('nix-meson-build-support/deps-lists')
subdir('nix-meson-build-support/common')

# clang-tidy plugins require LLVM (clang headers provided via buildInputs)
llvm_dep = dependency('LLVM', version : '>= 16', required : true)

sources = files(
  'NixClangTidyChecks.cc',
)

# Build as a shared module (plugin) that can be loaded by clang-tidy --load
nix_clang_tidy_plugin = shared_module(
  'nix-clang-tidy',
  sources,
  dependencies : [ llvm_dep ],
  install : true,
)
