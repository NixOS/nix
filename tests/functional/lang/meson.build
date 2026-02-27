# Language tests - discovered via Python script (cross-platform)

python = find_program('python3')
lang_test_runner = files('test.py')

lang_test_files_result = run_command(
  python,
  files('discover_tests.py'),
  meson.current_source_dir(),
  check : true,
)
lang_test_names = lang_test_files_result.stdout().strip().split('\n')

# Get exe_wrapper for cross-compilation (e.g. wine)
exe_wrapper_value = ''
can_run_tests = true
if meson.is_cross_build() and host_machine.system() == 'windows'
  exe_wrapper_prop = meson.get_external_property('exe_wrapper', '')
  if exe_wrapper_prop != ''
    exe_wrapper = find_program(exe_wrapper_prop, required : false)
  else
    # Fallback: try to find wine
    exe_wrapper = find_program('wine64', 'wine', required : false)
  endif
  if exe_wrapper.found()
    exe_wrapper_value = exe_wrapper.full_path()
  else
    # Can't run Windows executables without wine
    can_run_tests = false
  endif
endif

if not can_run_tests
  message('Skipping lang tests: cross-compiling for Windows but no exe_wrapper (wine) available')
  subdir_done()
endif

foreach test_name : lang_test_names

  asan_options = 'abort_on_error=1:print_summary=1:detect_leaks=0'
  asan_options += ':log_path=@0@'.format(meson.current_build_dir() / 'asan-log')

  test(
    test_name,
    python,
    args : [ lang_test_runner, test_name ],
    suite : 'lang',
    env : {
      'ASAN_OPTIONS' : asan_options,
      '_NIX_TEST_SOURCE_DIR' : meson.current_source_dir() / '..',
      '_NIX_TEST_BUILD_DIR' : meson.current_build_dir() / '..',
      '_NIX_TEST_HOST_OS' : host_machine.system(),
      '_NIX_TEST_EXE_WRAPPER' : exe_wrapper_value,
      'TEST_SUITE_NAME' : 'lang',
      'TEST_NAME' : test_name,
      'NIX_REMOTE' : '',
      'NIX_BIN_DIR' : nix_bin_dir,
    },
    timeout : 60,
    workdir : meson.current_source_dir() / '..',
  )
endforeach
