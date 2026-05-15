project(
  'nix-util-fuzz',
  'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    'warning_level=1',
  ],
  meson_version : '>= 1.8',
  license : 'LGPL-2.1-or-later',
)

cxx = meson.get_compiler('cpp')

# libFuzzer is clang-only; reject other compilers up front so the user
# doesn't have to decode a link error.
if cxx.get_id() != 'clang'
  error(
    'The fuzzers feature requires clang; got compiler id @0@. Re-run with CXX=clang++ (or use a clang stdenv).'
    .format(cxx.get_id()),
  )
endif

subdir('nix-meson-build-support/deps-lists')

deps_private_maybe_subproject = [
  dependency('nix-util'),
]
deps_public_maybe_subproject = []
subdir('nix-meson-build-support/subprojects')

# Resolve the fuzzing engine.
#
# OSS-Fuzz and similar drivers pass the engine in the `LIB_FUZZING_ENGINE`
# environment variable, either as a path to a prebuilt static library or
# as a compiler flag (e.g. `-fsanitize=fuzzer`). If unset, use clang's
# built-in libFuzzer so `-Dfuzzers=enabled` is enough for local runs.
fuzz_engine_env = run_command(
  find_program('sh'),
  '-c',
  'printf %s "${LIB_FUZZING_ENGINE:-}"',
  check : true,
).stdout()

if fuzz_engine_env != ''
  fuzz_engine_compile_args = []
  fuzz_engine_link_args = [ fuzz_engine_env ]
  message('Using LIB_FUZZING_ENGINE=@0@'.format(fuzz_engine_env))
else
  fuzz_engine_compile_args = [ '-fsanitize=fuzzer-no-link' ]
  fuzz_engine_link_args = [ '-fsanitize=fuzzer' ]
  message('LIB_FUZZING_ENGINE is unset; using clang\'s built-in libFuzzer.')
endif

# Pulls in project-wide warning flags, the `-shared-libasan` workaround
# for clang + sanitizers, and `__asan_default_options` (sets
# detect_leaks=0 etc.). Mirrors what the *-tests subprojects do.
subdir('nix-meson-build-support/common')

# Each target is one TU defining `LLVMFuzzerTestOneInput` (optionally
# also `LLVMFuzzerInitialize`). Installed so OSS-Fuzz's build.sh and
# downstream packagers can pick the binaries up next to the corpora.

executable(
  'fuzz_url',
  'fuzz_url.cc',
  dependencies : deps_private_subproject + deps_private + deps_other,
  cpp_args : fuzz_engine_compile_args,
  link_args : fuzz_engine_link_args,
  install : true,
)
