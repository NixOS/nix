# clang-tidy integration for the Nix project
#
# This provides the 'clang-tidy' run_target for per-component builds.
# The custom plugin (nix-clang-tidy) is built as a separate component
# and loaded automatically when available.

python = find_program('python3')
run_clang_tidy = find_program('run-clang-tidy', required : false)

# Resolves via override_dependency (monolithic) or pkg-config (per-component Nix)
nix_clang_tidy_plugin = dependency('nix-clang-tidy', required : false)

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
  # Pre-build generated headers/sources (.gen.hh, -tab.cc, etc.) so clang-tidy
  # can run on a fresh build dir without a full `ninja` first.
  # See: https://github.com/mesonbuild/meson/issues/12817
  build_gen_targets = custom_target(
    'build-gen-for-tidy',
    command : [
      python,
      files('build_required_targets.py'),
      meson.global_build_root(),
      '--stamp', '@OUTPUT@',
    ],
    output : 'gen-targets.stamp',
    build_always_stale : true,
    build_by_default : false,
  )

  # In monolithic builds the plugin .so is built from source — ensure it's
  # built before we try to -load it. In per-component builds the plugin is
  # already installed (buildInputs), so no extra dep is needed.
  clang_tidy_deps = [ build_gen_targets ]
  if is_variable('clang_tidy_plugin_sp') and clang_tidy_plugin_sp.found()
    clang_tidy_deps += [
      clang_tidy_plugin_sp.get_variable('nix_clang_tidy_plugin'),
    ]
  endif

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
      nix_clang_tidy_plugin.get_variable('plugin_path'),
    ]
  endif

  run_target(
    'clang-tidy',
    command : [
      python,
      run_clang_tidy_args,
      '--werror',
    ],
    depends : clang_tidy_deps,
  )

  run_target(
    'clang-tidy-fix',
    command : [
      python,
      run_clang_tidy_args,
      '--fix',
    ],
    depends : clang_tidy_deps,
  )
endif
