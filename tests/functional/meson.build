project(
  'nix-functional-tests',
  version : files('.version'),
  default_options : [
    'cpp_std=c++23',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.3',
  license : 'LGPL-2.1-or-later',
)

fs = import('fs')

nix = find_program('nix')
bash = find_program('bash', native : true)
busybox = find_program('busybox', native : true, required : false)
# Look up `coreutils` package by searching for `ls` binary.
# Previously we looked up `coreutils` on `linux`, but that is not
# guaranteed to exist either.
coreutils = find_program('ls', native : true)
dot = find_program('dot', native : true, required : false)

nix_bin_dir = fs.parent(nix.full_path())

subdir('nix-meson-build-support/default-system-cpu')

test_confdata = {
  'bindir' : nix_bin_dir,
  'coreutils' : fs.parent(coreutils.full_path()),
  'dot' : dot.found() ? dot.full_path() : '',
  'bash' : bash.full_path(),
  'sandbox_shell' : busybox.found() ? busybox.full_path() : '',
  'PACKAGE_VERSION' : meson.project_version(),
  'system' : nix_system_cpu + '-' + host_machine.system(),
}

# Just configures `common/vars-and-functions.sh.in`.
# Done as a subdir() so Meson places it under `common` in the build directory as well.
subdir('common')

configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

suites = [
  {
    'name' : 'main',
    'deps' : [],
    'tests' : [
      'absolute-path-literals.sh',
      'add.sh',
      'bash-profile.sh',
      'binary-cache-build-remote.sh',
      'binary-cache-compression.sh',
      'binary-cache.sh',
      'build-cores.sh',
      'build-delete.sh',
      'build-dry.sh',
      'build-remote-content-addressed-fixed.sh',
      'build-remote-content-addressed-floating.sh',
      'build-remote-input-addressed.sh',
      'build-remote-trustless-should-fail-0.sh',
      'build-remote-trustless-should-pass-0.sh',
      'build-remote-trustless-should-pass-1.sh',
      'build-remote-trustless-should-pass-2.sh',
      'build-remote-trustless-should-pass-3.sh',
      'build-remote-with-mounted-ssh-ng.sh',
      'build.sh',
      'characterisation-test-infra.sh',
      'check-refs.sh',
      'check-reqs.sh',
      'check.sh',
      'chroot-store.sh',
      'cli-characterisation.sh',
      'completions.sh',
      'compression-levels.sh',
      'config.sh',
      'db-migration.sh',
      'delete-no-keep.sh',
      'dependencies.sh',
      'derivation-advanced-attributes.sh',
      'derivation-json.sh',
      'dump-db.sh',
      'eval-store.sh',
      'eval.sh',
      'experimental-features.sh',
      'export-graph.sh',
      'export.sh',
      'external-builders.sh',
      'extra-sandbox-profile.sh',
      'fetchClosure.sh',
      'fetchGit.sh',
      'fetchGitRefs.sh',
      'fetchGitShallow.sh',
      'fetchGitSubmodules.sh',
      'fetchGitVerification.sh',
      'fetchMercurial.sh',
      'fetchPath.sh',
      'fetchTree-file.sh',
      'fetchurl.sh',
      'filter-source.sh',
      'fixed-slow-vs-bad.sh',
      'fixed.sh',
      'flamegraph-profiler.sh',
      'formatter.sh',
      'function-trace.sh',
      'gc-auto.sh',
      'gc-closure.sh',
      'gc-concurrent.sh',
      'gc-non-blocking.sh',
      'gc-runtime.sh',
      'gc.sh',
      'hash-convert.sh',
      'hash-path.sh',
      'help.sh',
      'import-from-derivation.sh',
      'impure-derivations.sh',
      'impure-env.sh',
      'impure-eval.sh',
      'json.sh',
      'lang-gc.sh',
      'lang.sh',
      'legacy-ssh-store.sh',
      'linux-sandbox.sh',
      'logging.sh',
      'make-content-addressed.sh',
      'misc.sh',
      'multiple-outputs-substitute-failure.sh',
      'multiple-outputs.sh',
      'nar-access.sh',
      'nars.sh',
      'nested-sandboxing.sh',
      'nix-build.sh',
      'nix-channel.sh',
      'nix-collect-garbage-d.sh',
      'nix-copy-ssh-ng.sh',
      'nix-copy-ssh.sh',
      'nix-profile.sh',
      'nix-shell.sh',
      'nix_path.sh',
      'no-url-literals.sh',
      'optimise-store.sh',
      'output-normalization.sh',
      'pass-as-file.sh',
      'path-from-hash-part.sh',
      'path-info.sh',
      'placeholders.sh',
      'post-hook.sh',
      'pure-eval.sh',
      'read-only-store.sh',
      'readfile-context.sh',
      'recursive.sh',
      'referrers.sh',
      'remote-store.sh',
      'repair.sh',
      'repl.sh',
      'restricted.sh',
      'search.sh',
      'secure-drv-outputs.sh',
      'selfref-gc.sh',
      'shell.sh',
      'short-path-literals.sh',
      'signing.sh',
      'simple.sh',
      'ssh-relay.sh',
      'store-info.sh',
      'store-print-env.sh',
      'structured-attrs.sh',
      'substitute-with-invalid-ca.sh',
      'suggestions.sh',
      'supplementary-groups.sh',
      'symlinks.sh',
      'tarball.sh',
      'test-infra.sh',
      'timeout.sh',
      'toString-path.sh',
      'user-envs-migration.sh',
      'user-envs.sh',
      'why-depends.sh',
    ],
    'workdir' : meson.current_source_dir(),
  },
]

nix_store = dependency('nix-store', required : false)
if nix_store.found()
  add_languages('cpp')
  subdir('test-libstoreconsumer')
  suites += {
    'name' : 'libstoreconsumer',
    'deps' : [
      libstoreconsumer_tester,
    ],
    'tests' : [
      'test-libstoreconsumer.sh',
    ],
    'workdir' : meson.current_source_dir(),
  }

endif

# Plugin tests require shared libraries support.
nix_expr = dependency('nix-expr', required : false)
if nix_expr.found() and get_option('default_library') != 'static'
  add_languages('cpp')
  subdir('plugins')
  plugin_test_deps = [ libplugintest ]
  if get_option('plugin-c-api') and not host_machine.system().startswith(
    'windows',
  )
    plugin_test_deps += [ libplugintest_dlsym ]
  endif
  suites += {
    'name' : 'plugins',
    'deps' : plugin_test_deps,
    'tests' : [
      'plugins.sh',
    ],
    'workdir' : meson.current_source_dir(),
  }
endif

subdir('ca')
subdir('dyn-drv')
subdir('fetchers')
subdir('flakes')
subdir('git')
subdir('git-hashing')
subdir('local-overlay-store')

foreach suite : suites
  workdir = suite['workdir']
  suite_name = suite['name']
  # This is workaround until [1] is resolved. When building in a devshell
  # as a subproject we want the tests to depend on the nix build target, so
  # that it gets automatically rebuilt.
  # However, when the functional test suite is built separately (via componentized
  # builds or in NixOS tests) we can't depend on the nix executable, since it's
  # an external program. The following is a simple heuristic that suffices for now.
  # [1]: https://github.com/mesonbuild/meson/issues/13877
  deps = suite['deps']
  if meson.is_subproject()
    nix_subproject = subproject('nix')
    deps += [ nix ] + nix_subproject.get_variable('nix_symlinks_targets')
  endif
  foreach script : suite['tests']
    # Turns, e.g., `tests/functional/flakes/show.sh` into a Meson test target called
    # `functional-flakes-show`.
    name = fs.replace_suffix(script, '')
    asan_options = 'abort_on_error=1:print_summary=1:detect_leaks=0'
    # Otherwise ASAN dumps warnings into stderr that make some tests fail on stderr output
    # comparisons.
    asan_options += ':log_path=@0@'.format(
      meson.current_build_dir() / 'asan-log',
    )

    # TODO: use Meson's 'should_fail' with a custom exit code once we require
    # Meson >= 1.11.0 for proper expected failure support. For now, tests that
    # demonstrate known bugs use skipTest in the test script itself.

    test(
      name,
      bash,
      args : [
        '-x',
        '-e',
        '-u',
        '-o',
        'pipefail',
        script,
      ],
      suite : suite_name,
      env : {
        'ASAN_OPTIONS' : asan_options,
        '_NIX_TEST_SOURCE_DIR' : meson.current_source_dir(),
        '_NIX_TEST_BUILD_DIR' : meson.current_build_dir(),
        'TEST_SUITE_NAME' : suite_name,
        'TEST_NAME' : name,
        'NIX_REMOTE' : '',
        'PS4' : '+(${BASH_SOURCE[0]-$0}:$LINENO) ',
      },
      # Some tests take 15+ seconds even on an otherwise idle machine;
      # on a loaded machine this can easily drive them to failure. Give
      # them more time than the default of 30 seconds.
      timeout : 300,
      # Used for target dependency/ordering tracking, not adding compiler flags or anything.
      depends : deps,
      workdir : workdir,
    )
  endforeach
endforeach
