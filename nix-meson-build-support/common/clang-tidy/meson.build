# clang-tidy integration for the Nix project
#
# This provides the 'clang-tidy' run_target for per-component builds.
# The custom plugin (nix-clang-tidy) is built as a separate component
# and loaded automatically when available.

python = find_program('python3')
run_clang_tidy = find_program('run-clang-tidy', required : false)

# Find the custom clang-tidy plugin if available
nix_clang_tidy_plugin = cxx.find_library('nix-clang-tidy', required : false)

# Clean the compilation database by stripping PCH arguments.
# This is needed because clang-tidy cannot process PCH flags from cc-wrapper.
# See: https://github.com/mesonbuild/meson/issues/13499
meson.add_postconf_script(
  python,
  meson.current_source_dir() / 'clean_compdb.py',
  meson.global_build_root() / 'compile_commands.json',
  meson.current_build_dir() / 'compile_commands.json',
)

if run_clang_tidy.found()
  run_clang_tidy_args = [
    meson.current_source_dir() / 'clang-tidy-runner.py',
    '--run-clang-tidy-path',
    run_clang_tidy,
    '--compdb-path',
    meson.current_build_dir(),
    '--config-file',
    meson.current_source_dir() / '.clang-tidy',
  ]

  if nix_clang_tidy_plugin.found()
    run_clang_tidy_args += [
      '--plugin-path',
      nix_clang_tidy_plugin.full_path(),
    ]
  endif

  run_target(
    'clang-tidy',
    command : [
      python,
      run_clang_tidy_args,
      '--werror',
    ],
  )

  run_target(
    'clang-tidy-fix',
    command : [
      python,
      run_clang_tidy_args,
      '--fix',
    ],
  )
endif
