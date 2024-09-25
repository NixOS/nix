project('nix-functional-tests', 'cpp',
  version : files('.version'),
  default_options : [
    'cpp_std=c++2a',
    # TODO(Qyriad): increase the warning level
    'warning_level=1',
    'debug=true',
    'optimization=2',
    'errorlogs=true', # Please print logs for tests that fail
  ],
  meson_version : '>= 1.3',
  license : 'LGPL-2.1-or-later',
)

fs = import('fs')

# Need to combine source and build trees
run_command(
  'rsync',
  '-a',
  '--copy-unsafe-links',
  meson.current_source_dir() / '',
  meson.current_build_dir() / '',
)
# This current-source-escaping relative is no good because we don't know
# where the build directory will be, therefore we fix it up. Once the
# Make build system is gone, we should think about doing this better.
scripts_dir = fs.relative_to(
  meson.current_source_dir() / '..' / '..' / 'scripts',
  meson.current_build_dir(),
)
run_command(
  'sed',
  '-i', meson.current_build_dir() / 'bash-profile.sh',
  '-e', 's^../../scripts^@0@^'.format(scripts_dir),
)

nix = find_program('nix')
bash = find_program('bash', native : true)
busybox = find_program('busybox', native : true, required : false)
coreutils = find_program('coreutils', native : true)
dot = find_program('dot', native : true, required : false)

nix_bin_dir = fs.parent(nix.full_path())

test_confdata = {
  'bindir': nix_bin_dir,
  'coreutils': fs.parent(coreutils.full_path()),
  'dot': dot.found() ? dot.full_path() : '',
  'bash': bash.full_path(),
  'sandbox_shell': busybox.found() ? busybox.full_path() : '',
  'PACKAGE_VERSION': meson.project_version(),
  'system': host_machine.cpu_family() + '-' + host_machine.system(),
}

# Just configures `common/vars-and-functions.sh.in`.
# Done as a subdir() so Meson places it under `common` in the build directory as well.
subdir('common')

config_nix_in = configure_file(
  input : 'config.nix.in',
  output : 'config.nix',
  configuration : test_confdata,
)

suites = [
  {
    'name' : 'main',
    'deps': [],
    'tests': [
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
      'fmt.sh',
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
      'toString-path.sh',
      'read-only-store.sh',
      'nested-sandboxing.sh',
      'impure-env.sh',
      'debugger.sh',
      'extra-sandbox-profile.sh',
      'help.sh',
    ],
    'workdir': meson.current_build_dir(),
  },
]

nix_store = dependency('nix-store', required : false)
if nix_store.found()
  subdir('test-libstoreconsumer')
  suites += {
    'name': 'libstoreconsumer',
    'deps': [
      libstoreconsumer_tester,
    ],
    'tests': [
      'test-libstoreconsumer.sh',
    ],
    'workdir': meson.current_build_dir(),
  }

endif

# Plugin tests require shared libraries support.
nix_expr = dependency('nix-expr', required : false)
if nix_expr.found() and get_option('default_library') != 'static'
  subdir('plugins')
  suites += {
    'name': 'plugins',
    'deps': [
      libplugintest,
    ],
    'tests': [
      'plugins.sh',
    ],
    'workdir': meson.current_build_dir(),
  }
endif

subdir('ca')
subdir('dyn-drv')
subdir('flakes')
subdir('git-hashing')
subdir('local-overlay-store')

foreach suite : suites
  foreach script : suite['tests']
    workdir = suite['workdir']
    prefix = fs.relative_to(workdir, meson.project_build_root())

    script = script
    # Turns, e.g., `tests/functional/flakes/show.sh` into a Meson test target called
    # `functional-flakes-show`.
    name = fs.replace_suffix(prefix / script, '')

    test(
      name,
      bash,
      args: [
        '-x',
        '-e',
        '-u',
        '-o', 'pipefail',
        script,
      ],
      suite : suite['name'],
      env : {
        'TEST_NAME': name,
        'NIX_REMOTE': '',
        'PS4': '+(${BASH_SOURCE[0]-$0}:$LINENO) ',
      },
      # some tests take 15+ seconds even on an otherwise idle machine, on a loaded machine
      # this can easily drive them to failure. give them more time than default of 30sec
      timeout : 300,
      # Used for target dependency/ordering tracking, not adding compiler flags or anything.
      depends : suite['deps'],
      workdir : workdir,
      # Won't pass until man pages are generated
      should_fail : suite['name'] == 'main' and script == 'help.sh'
    )
  endforeach
endforeach
