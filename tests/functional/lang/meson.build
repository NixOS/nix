# Language tests - discovered via glob

python = find_program('python3')
lang_test_runner = files('test.py')

# Meson doesn't have glob, so shell out to find test files
# Only include formulaic tests (parse-fail-*, parse-okay-*, eval-fail-*, eval-okay-*)
lang_test_files_result = run_command(
  'sh',
  '-c',
  'cd "$1" && ls -1 parse-fail-*.nix parse-okay-*.nix eval-fail-*.nix eval-okay-*.nix 2>/dev/null | sed "s/\\.nix$//"',
  '--',
  meson.current_source_dir(),
  check : true,
)
lang_test_names = lang_test_files_result.stdout().strip().split('\n')

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
      'TEST_SUITE_NAME' : 'lang',
      'TEST_NAME' : test_name,
      'NIX_REMOTE' : '',
      'NIX_BIN_DIR' : nix_bin_dir,
    },
    timeout : 60,
    workdir : meson.current_source_dir() / '..',
  )
endforeach
