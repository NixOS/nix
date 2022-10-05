Describe 'migration tests scripts'

  setup() {
      export NIX_REMOTE=""
      export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
      . ./common.sh
      bash init.sh
      clearStore
      clearCache
  }
  BeforeEach 'setup'

  It 'gc.sh'
    When run command bash -e gc.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'remote-store.sh'
    When run command bash -e remote-store.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'lang.sh'
    When run command bash -e lang.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchMercurial.sh'
    When run command bash -e fetchMercurial.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'gc-auto.sh'
    When run command bash -e gc-auto.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'user-envs.sh'
    When run command bash -e user-envs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'binary-cache.sh'
    When run command bash -e binary-cache.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'multiple-outputs.sh'
    When run command bash -e multiple-outputs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'nix-build.sh'
    When run command bash -e nix-build.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'gc-concurrent.sh'
    When run command bash -e gc-concurrent.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'repair.sh'
    When run command bash -e repair.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fixed.sh'
    When run command bash -e fixed.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'export-graph.sh'
    When run command bash -e export-graph.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'timeout.sh'
    When run command bash -e timeout.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchGitRefs.sh'
    When run command bash -e fetchGitRefs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'gc-runtime.sh'
    When run command bash -e gc-runtime.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'tarball.sh'
    When run command bash -e tarball.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchGit.sh'
    When run command bash -e fetchGit.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchurl.sh'
    When run command bash -e fetchurl.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchPath.sh'
    When run command bash -e fetchPath.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'fetchTree-file.sh'
    When run command bash -e fetchTree-file.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'simple.sh'
    When run command bash -e simple.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'referrers.sh'
    When run command bash -e referrers.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'optimise-store.sh'
    When run command bash -e optimise-store.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'substitute-with-invalid-ca.sh'
    When run command bash -e substitute-with-invalid-ca.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'signing.sh'
    When run command bash -e signing.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'hash.sh'
    When run command bash -e hash.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'gc-non-blocking.sh'
    When run command bash -e gc-non-blocking.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'check.sh'
    When run command bash -e check.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'nix-shell.sh'
    When run command bash -e nix-shell.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'check-refs.sh'
    When run command bash -e check-refs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'build-remote-input-addressed.sh'
    When run command bash -e build-remote-input-addressed.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'secure-drv-outputs.sh'
    When run command bash -e secure-drv-outputs.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'restricted.sh'
    When run command bash -e restricted.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchGitSubmodules.sh'
    When run command bash -e fetchGitSubmodules.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'readfile-context.sh'
    When run command bash -e readfile-context.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'nix-channel.sh'
    When run command bash -e nix-channel.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'recursive.sh'
    When run command bash -e recursive.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'dependencies.sh'
    When run command bash -e dependencies.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'check-reqs.sh'
    When run command bash -e check-reqs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'build-remote-content-addressed-fixed.sh'
    When run command bash -e build-remote-content-addressed-fixed.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'build-remote-content-addressed-floating.sh'
    When run command bash -e build-remote-content-addressed-floating.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'nar-access.sh'
    When run command bash -e nar-access.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'pure-eval.sh'
    When run command bash -e pure-eval.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'eval.sh'
    When run command bash -e eval.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'repl.sh'
    When run command bash -e repl.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'binary-cache-build-remote.sh'
    When run command bash -e binary-cache-build-remote.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'logging.sh'
    When run command bash -e logging.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'export.sh'
    When run command bash -e export.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'config.sh'
    When run command bash -e config.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'add.sh'
    When run command bash -e add.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'local-store.sh'
    When run command bash -e local-store.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'filter-source.sh'
    When run command bash -e filter-source.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'misc.sh'
    When run command bash -e misc.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'dump-db.sh'
    When run command bash -e dump-db.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'linux-sandbox.sh'
    When run command bash -e linux-sandbox.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'build-dry.sh'
    When run command bash -e build-dry.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'structured-attrs.sh'
    When run command bash -e structured-attrs.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'shell.sh'
    When run command bash -e shell.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'brotli.sh'
    When run command bash -e brotli.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'zstd.sh'
    When run command bash -e zstd.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'compression-levels.sh'
    When run command bash -e compression-levels.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'nix-copy-ssh.sh'
    When run command bash -e nix-copy-ssh.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'post-hook.sh'
    When run command bash -e post-hook.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'function-trace.sh'
    When run command bash -e function-trace.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'fmt.sh'
    When run command bash -e fmt.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'eval-store.sh'
    When run command bash -e eval-store.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'why-depends.sh'
    When run command bash -e why-depends.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'import-derivation.sh'
    When run command bash -e import-derivation.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'nix_path.sh'
    When run command bash -e nix_path.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'case-hack.sh'
    When run command bash -e case-hack.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'placeholders.sh'
    When run command bash -e placeholders.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'ssh-relay.sh'
    When run command bash -e ssh-relay.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'plugins.sh'
    When run command bash -e plugins.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'build.sh'
    When run command bash -e build.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'selfref-gc.sh'
    When run command bash -e selfref-gc.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  # Only run this if we have an older Nix available
  It 'db-migration.sh'
    When run command bash -e db-migration.sh
    The status should be failure
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'bash-profile.sh'
    When run command bash -e bash-profile.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'pass-as-file.sh'
    When run command bash -e pass-as-file.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should not equal 0
  End

  It 'describe-stores.sh'
    When run command bash -e describe-stores.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'nix-profile.sh'
    When run command bash -e nix-profile.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'suggestions.sh'
    When run command bash -e suggestions.sh
    The status should be success
    The lines of stdout should equal 0
    The lines of stderr should equal 0
  End

  It 'store-ping.sh'
    When run command bash -e store-ping.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'fetchClosure.sh'
    When run command bash -e fetchClosure.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

  It 'completions.sh'
    When run command bash -e completions.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should equal 0
  End

  It 'impure-derivations.sh'
    When run command bash -e impure-derivations.sh
    The status should be success
    The lines of stdout should not equal 0
    The lines of stderr should not equal 0
  End

End
