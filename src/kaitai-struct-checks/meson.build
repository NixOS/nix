# Run with:
#     meson test --suite kaitai-struct
# Run with: (without shell / configure)
#     nix build .#nix-kaitai-struct-checks

project(
  'nix-kaitai-struct-checks',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.1',
  license : 'LGPL-2.1-or-later',
)

kaitai_runtime_dep = dependency('kaitai-struct-cpp-stl-runtime', required : true)
gtest_dep = dependency('gtest')
gtest_main_dep = dependency('gtest_main', required : true)

# Find the Kaitai Struct compiler
ksc = find_program('ksc', required : true)

kaitai_generated_srcs = custom_target(
  'kaitai-generated-sources',
  input : [ 'nar.ksy' ],
  output : [ 'nix_nar.cpp', 'nix_nar.h' ],
  command : [
    ksc,
    '@INPUT@',
    '--target', 'cpp_stl',
    '--outdir',
    meson.current_build_dir(),
  ],
)

nar_kaitai_lib = library(
  'nix-nar-kaitai-lib',
  kaitai_generated_srcs,
  dependencies : [ kaitai_runtime_dep ],
  install : true,
)

nar_kaitai_dep = declare_dependency(
  link_with : nar_kaitai_lib,
  sources : kaitai_generated_srcs[1],
)

# The nar directory is a committed symlink to the actual nars location
nars_dir = meson.current_source_dir() / 'nars'

# Get all example files
nars = [
  'dot.nar',
]

test_deps = [
  nar_kaitai_dep,
  kaitai_runtime_dep,
  gtest_main_dep,
]

this_exe = executable(
  meson.project_name(),
  'test-parse-nar.cc',
  dependencies : test_deps,
)

test(
  meson.project_name(),
  this_exe,
  env : [ 'NIX_NARS_DIR=' + nars_dir ],
  protocol : 'gtest',
)
