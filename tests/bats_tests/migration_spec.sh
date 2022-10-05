setup() {
    bats_load_library bats-support
    bats_load_library bats-assert
    bats_require_minimum_version 1.5.0

    export NIX_REMOTE=""
    export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
    . ./common.sh
    bash init.sh
    clearStore
    clearCache
}

@test 'build-remote-content-addressed-floating.sh' {
  run bash -e build-remote-content-addressed-floating.sh
  [ "$status" -eq 0 ]
}
@test 'nar-access.sh' {
  run bash -e nar-access.sh
  [ "$status" -eq 0 ]
}
@test 'pure-eval.sh' {
  run bash -e pure-eval.sh
  [ "$status" -eq 0 ]
}
@test 'eval.sh' {
  run bash -e eval.sh
  [ "$status" -eq 0 ]
}
@test 'repl.sh' {
  run bash -e repl.sh
  [ "$status" -eq 0 ]
}
@test 'binary-cache-build-remote.sh' {
  run bash -e binary-cache-build-remote.sh
  [ "$status" -eq 0 ]
}
@test 'logging.sh' {
  run bash -e logging.sh
  [ "$status" -eq 0 ]
}
@test 'export.sh' {
  run bash -e export.sh
  [ "$status" -eq 0 ]
}
@test 'config.sh' {
  run bash -e config.sh
  [ "$status" -eq 0 ]
}
@test 'add.sh' {
  run bash -e add.sh
  [ "$status" -eq 0 ]
}
@test 'local-store.sh' {
  run bash -e local-store.sh
  [ "$status" -eq 0 ]
}
@test 'filter-source.sh' {
  run bash -e filter-source.sh
  [ "$status" -eq 0 ]
}
@test 'misc.sh' {
  run bash -e misc.sh
  [ "$status" -eq 0 ]
}
@test 'dump-db.sh' {
  run bash -e dump-db.sh
  [ "$status" -eq 0 ]
}
@test 'linux-sandbox.sh' {
  run bash -e linux-sandbox.sh
  [ "$status" -eq 0 ]
}
@test 'build-dry.sh' {
  run bash -e build-dry.sh
  [ "$status" -eq 0 ]
}
@test 'structured-attrs.sh' {
  run bash -e structured-attrs.sh
  [ "$status" -eq 0 ]
}
@test 'shell.sh' {
  run bash -e shell.sh
  [ "$status" -eq 0 ]
}
@test 'brotli.sh' {
  run bash -e brotli.sh
  [ "$status" -eq 0 ]
}
@test 'zstd.sh' {
  run bash -e zstd.sh
  [ "$status" -eq 0 ]
}
@test 'compression-levels.sh' {
  run bash -e compression-levels.sh
  [ "$status" -eq 0 ]
}
@test 'nix-copy-ssh.sh' {
  run bash -e nix-copy-ssh.sh
  [ "$status" -eq 0 ]
}
@test 'post-hook.sh' {
  run bash -e post-hook.sh
  [ "$status" -eq 0 ]
}
@test 'function-trace.sh' {
  run bash -e function-trace.sh
  [ "$status" -eq 0 ]
}
@test 'fmt.sh' {
  run bash -e fmt.sh
  [ "$status" -eq 0 ]
}
@test 'eval-store.sh' {
  run bash -e eval-store.sh
  [ "$status" -eq 0 ]
}
@test 'why-depends.sh' {
  run bash -e why-depends.sh
  [ "$status" -eq 0 ]
}
@test 'import-derivation.sh' {
  run bash -e import-derivation.sh
  [ "$status" -eq 0 ]
}
@test 'nix_path.sh' {
  run bash -e nix_path.sh
  [ "$status" -eq 0 ]
}
@test 'case-hack.sh' {
  run bash -e case-hack.sh
  [ "$status" -eq 0 ]
}
@test 'placeholders.sh' {
  run bash -e placeholders.sh
  [ "$status" -eq 0 ]
}
@test 'ssh-relay.sh' {
  run bash -e ssh-relay.sh
  [ "$status" -eq 0 ]
}
@test 'plugins.sh' {
  run bash -e plugins.sh
  [ "$status" -eq 0 ]
}
@test 'build.sh' {
  run bash -e build.sh
  [ "$status" -eq 0 ]
}
@test 'selfref-gc.sh' {
  run bash -e selfref-gc.sh
  [ "$status" -eq 0 ]
}
@test 'db-migration.sh' {
  run bash -e db-migration.sh
}
@test 'bash-profile.sh' {
  run bash -e bash-profile.sh
  [ "$status" -eq 0 ]
}
@test 'pass-as-file.sh' {
  run bash -e pass-as-file.sh
  [ "$status" -eq 0 ]
}
@test 'describe-stores.sh' {
  run bash -e describe-stores.sh
  [ "$status" -eq 0 ]
}
@test 'nix-profile.sh' {
  run bash -e nix-profile.sh
  [ "$status" -eq 0 ]
}
@test 'suggestions.sh' {
  run bash -e suggestions.sh
  [ "$status" -eq 0 ]
}
@test 'store-ping.sh' {
  run bash -e store-ping.sh
  [ "$status" -eq 0 ]
}
@test 'fetchClosure.sh' {
  run bash -e fetchClosure.sh
  [ "$status" -eq 0 ]
}
@test 'completions.sh' {
  run bash -e completions.sh
  [ "$status" -eq 0 ]
}
@test 'impure-derivations.sh' {
  run bash -e impure-derivations.sh
  [ "$status" -eq 0 ]
}
@test 'gc.sh' {
    run bash -e gc.sh
}
@test 'remote-store.sh' {
  run bash -e remote-store.sh
  [ "$status" -eq 0 ]
}
@test 'lang.sh' {
  run bash -e lang.sh
  [ "$status" -eq 0 ]
}
@test 'fetchMercurial.sh' {
  run bash -e fetchMercurial.sh
  [ "$status" -eq 0 ]
}
@test 'gc-auto.sh' {
  run bash -e gc-auto.sh
  [ "$status" -eq 0 ]
}
@test 'user-envs.sh' {
  run bash -e user-envs.sh
  [ "$status" -eq 0 ]
}
@test 'binary-cache.sh' {
  run bash -e binary-cache.sh
  [ "$status" -eq 0 ]
}
@test 'multiple-outputs.sh' {
  run bash -e multiple-outputs.sh
  [ "$status" -eq 0 ]
}
@test 'nix-build.sh' {
  run bash -e nix-build.sh
  [ "$status" -eq 0 ]
}
@test 'gc-concurrent.sh' {
  run bash -e gc-concurrent.sh
  [ "$status" -eq 0 ]
}
@test 'repair.sh' {
  run bash -e repair.sh
  [ "$status" -eq 0 ]
}
@test 'fixed.sh' {
  run bash -e fixed.sh
  [ "$status" -eq 0 ]
}
@test 'export-graph.sh' {
  run bash -e export-graph.sh
  [ "$status" -eq 0 ]
}
@test 'timeout.sh' {
  run bash -e timeout.sh
  [ "$status" -eq 0 ]
}
@test 'fetchGitRefs.sh' {
  run bash -e fetchGitRefs.sh
  [ "$status" -eq 0 ]
}
@test 'gc-runtime.sh' {
  run bash -e gc-runtime.sh
  [ "$status" -eq 0 ]
}
@test 'tarball.sh' {
  run bash -e tarball.sh
  [ "$status" -eq 0 ]
}
@test 'fetchGit.sh' {
  run bash -e fetchGit.sh
  [ "$status" -eq 0 ]
}
@test 'fetchurl.sh' {
  run bash -e fetchurl.sh
  [ "$status" -eq 0 ]
}
@test 'fetchPath.sh' {
  run bash -e fetchPath.sh
  [ "$status" -eq 0 ]
}
@test 'fetchTree-file.sh' {
  run bash -e fetchTree-file.sh
  [ "$status" -eq 0 ]
}
@test 'simple.sh' {
  run bash -e simple.sh
  [ "$status" -eq 0 ]
}
@test 'referrers.sh' {
  run bash -e referrers.sh
  [ "$status" -eq 0 ]
}
@test 'optimise-store.sh' {
  run bash -e optimise-store.sh
  [ "$status" -eq 0 ]
}
@test 'substitute-with-invalid-ca.sh' {
  run bash -e substitute-with-invalid-ca.sh
  [ "$status" -eq 0 ]
}
@test 'signing.sh' {
  run bash -e signing.sh
  [ "$status" -eq 0 ]
}
@test 'hash.sh' {
  run bash -e hash.sh
  [ "$status" -eq 0 ]
}
@test 'gc-non-blocking.sh' {
  run bash -e gc-non-blocking.sh
  [ "$status" -eq 0 ]
}
@test 'check.sh' {
  run bash -e check.sh
  [ "$status" -eq 0 ]
}
@test 'nix-shell.sh' {
  run bash -e nix-shell.sh
  [ "$status" -eq 0 ]
}
@test 'check-refs.sh' {
  run bash -e check-refs.sh
  [ "$status" -eq 0 ]
}
@test 'build-remote-input-addressed.sh' {
  run bash -e build-remote-input-addressed.sh
  [ "$status" -eq 0 ]
}
@test 'secure-drv-outputs.sh' {
  run bash -e secure-drv-outputs.sh
  [ "$status" -eq 0 ]
}
@test 'restricted.sh' {
  run bash -e restricted.sh
  [ "$status" -eq 0 ]
}
@test 'fetchGitSubmodules.sh' {
  run bash -e fetchGitSubmodules.sh
  [ "$status" -eq 0 ]
}
@test 'readfile-context.sh' {
  run bash -e readfile-context.sh
  [ "$status" -eq 0 ]
}
@test 'nix-channel.sh' {
  run bash -e nix-channel.sh
  [ "$status" -eq 0 ]
}
@test 'recursive.sh' {
  run bash -e recursive.sh
  [ "$status" -eq 0 ]
}
@test 'dependencies.sh' {
  run bash -e dependencies.sh
  [ "$status" -eq 0 ]
}
@test 'check-reqs.sh' {
  run bash -e check-reqs.sh
  [ "$status" -eq 0 ]
}
@test 'build-remote-content-addressed-fixed.sh' {
  run bash -e build-remote-content-addressed-fixed.sh
  [ "$status" -eq 0 ]
}
