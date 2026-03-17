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
  'format-string-arity.cc',
  'nix-clang-tidy-checks.cc',
)

# Build as a shared module (plugin) that can be loaded by clang-tidy --load
nix_clang_tidy_plugin = shared_module(
  'nix-clang-tidy',
  sources,
  dependencies : [ llvm_dep ],
  install : true,
)

# Export plugin path for consumers (nix-meson-build-support/common/clang-tidy/meson.build).
# In monolithic builds: override_dependency provides the build-dir path.
# In per-component Nix builds: the .pc file provides the installed path.
meson.override_dependency(
  'nix-clang-tidy',
  declare_dependency(
    variables : {'plugin_path' : nix_clang_tidy_plugin.full_path()},
  ),
)
import('pkgconfig').generate(
  nix_clang_tidy_plugin,
  filebase : 'nix-clang-tidy',
  name : 'nix-clang-tidy',
  description : 'Custom clang-tidy checks for Nix',
  variables : {
    'plugin_path' : get_option('prefix') / get_option('libdir') / 'libnix-clang-tidy.so',
  },
)
