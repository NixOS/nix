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
      'test-infra.sh',
      'gc.sh',
      'nix-collect-garbage-d.sh',
      'remote-store.sh',
      'legacy-ssh-store.sh',
      'lang.sh',
      'lang-gc.sh',
      'characterisation-test-infra.sh',
      'experimental-features.sh',
      'fetchMercurial.sh',
      'gc-auto.sh',
      'user-envs.sh',
      'user-envs-migration.sh',
      'binary-cache.sh',
      'multiple-outputs.sh',
      'nix-build.sh',
      'gc-concurrent.sh',
      'repair.sh',
      'fixed.sh',
      'export-graph.sh',
      'timeout.sh',
      'fetchGitRefs.sh',
      'gc-runtime.sh',
      'tarball.sh',
      'fetchGit.sh',
      'fetchGitShallow.sh',
      'fetchurl.sh',
      'fetchPath.sh',
      'fetchTree-file.sh',
      'simple.sh',
      'referrers.sh',
      'optimise-store.sh',
      'substitute-with-invalid-ca.sh',
      'signing.sh',
      'hash-convert.sh',
      'hash-path.sh',
      'gc-non-blocking.sh',
      'check.sh',
      'nix-shell.sh',
      'check-refs.sh',
      'build-remote-input-addressed.sh',
      'secure-drv-outputs.sh',
      'restricted.sh',
      'fetchGitSubmodules.sh',
      'fetchGitVerification.sh',
      'readfile-context.sh',
      'nix-channel.sh',
      'recursive.sh',
      'dependencies.sh',
      'check-reqs.sh',
      'build-remote-content-addressed-fixed.sh',
      'build-remote-content-addressed-floating.sh',
      'build-remote-trustless-should-pass-0.sh',
      'build-remote-trustless-should-pass-1.sh',
      'build-remote-trustless-should-pass-2.sh',
      'build-remote-trustless-should-pass-3.sh',
      'build-remote-trustless-should-fail-0.sh',
      'build-remote-with-mounted-ssh-ng.sh',
      'nar-access.sh',
      'impure-eval.sh',
      'pure-eval.sh',
      'eval.sh',
      'short-path-literals.sh',
      'no-url-literals.sh',
      'repl.sh',
      'binary-cache-build-remote.sh',
      'search.sh',
      'logging.sh',
      'export.sh',
      'config.sh',
      'add.sh',
      'chroot-store.sh',
      'filter-source.sh',
      'misc.sh',
      'dump-db.sh',
      'linux-sandbox.sh',
      'supplementary-groups.sh',
      'build-dry.sh',
      'structured-attrs.sh',
      'shell.sh',
      'brotli.sh',
      'zstd.sh',
      'compression-levels.sh',
      'nix-copy-ssh.sh',
      'nix-copy-ssh-ng.sh',
      'post-hook.sh',
      'function-trace.sh',
      'formatter.sh',
      'flamegraph-profiler.sh',
      'eval-store.sh',
      'why-depends.sh',
      'derivation-json.sh',
      'derivation-advanced-attributes.sh',
      'import-from-derivation.sh',
      'nix_path.sh',
      'nars.sh',
      'placeholders.sh',
      'ssh-relay.sh',
      'build.sh',
      'build-cores.sh',
      'build-delete.sh',
      'output-normalization.sh',
      'selfref-gc.sh',
      'db-migration.sh',
      'bash-profile.sh',
      'pass-as-file.sh',
      'nix-profile.sh',
      'suggestions.sh',
      'store-info.sh',
      'fetchClosure.sh',
      'completions.sh',
      'impure-derivations.sh',
      'path-from-hash-part.sh',
      'path-info.sh',
      'json.sh',
      'toString-path.sh',
      'read-only-store.sh',
      'nested-sandboxing.sh',
      'impure-env.sh',
      'debugger.sh',
      'extra-sandbox-profile.sh',
      'help.sh',
      'symlinks.sh',
      'external-builders.sh',
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
  suites += {
    'name' : 'plugins',
    'deps' : [
      libplugintest,
    ],
    'tests' : [
      'plugins.sh',
    ],
    'workdir' : meson.current_source_dir(),
  }
endif

subdir('ca')
subdir('dyn-drv')
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
