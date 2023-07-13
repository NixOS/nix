nix_tests = \
  test-infra.sh \
  init.sh \
  flakes/flakes.sh \
  flakes/run.sh \
  flakes/mercurial.sh \
  flakes/circular.sh \
  flakes/init.sh \
  flakes/inputs.sh \
  flakes/follow-paths.sh \
  flakes/bundle.sh \
  flakes/check.sh \
  flakes/unlocked-override.sh \
  flakes/absolute-paths.sh \
  flakes/build-paths.sh \
  flakes/flake-in-submodule.sh \
  gc.sh \
  nix-collect-garbage-d.sh \
  remote-store.sh \
  legacy-ssh-store.sh \
  lang.sh \
  lang-test-infra.sh \
  experimental-features.sh \
  fetchMercurial.sh \
  gc-auto.sh \
  user-envs.sh \
  user-envs-migration.sh \
  binary-cache.sh \
  multiple-outputs.sh \
  nix-build.sh \
  gc-concurrent.sh \
  repair.sh \
  fixed.sh \
  export-graph.sh \
  timeout.sh \
  fetchGitRefs.sh \
  gc-runtime.sh \
  tarball.sh \
  fetchGit.sh \
  fetchurl.sh \
  fetchPath.sh \
  fetchTree-file.sh \
  simple.sh \
  referrers.sh \
  optimise-store.sh \
  substitute-with-invalid-ca.sh \
  signing.sh \
  hash.sh \
  gc-non-blocking.sh \
  check.sh \
  nix-shell.sh \
  check-refs.sh \
  build-remote-input-addressed.sh \
  secure-drv-outputs.sh \
  restricted.sh \
  fetchGitSubmodules.sh \
  flakes/search-root.sh \
  readfile-context.sh \
  nix-channel.sh \
  recursive.sh \
  dependencies.sh \
  check-reqs.sh \
  build-remote-content-addressed-fixed.sh \
  build-remote-content-addressed-floating.sh \
  build-remote-trustless-should-pass-0.sh \
  build-remote-trustless-should-pass-1.sh \
  build-remote-trustless-should-pass-2.sh \
  build-remote-trustless-should-pass-3.sh \
  build-remote-trustless-should-fail-0.sh \
  nar-access.sh \
  pure-eval.sh \
  eval.sh \
  repl.sh \
  binary-cache-build-remote.sh \
  search.sh \
  logging.sh \
  export.sh \
  config.sh \
  add.sh \
  local-store.sh \
  filter-source.sh \
  misc.sh \
  dump-db.sh \
  linux-sandbox.sh \
  supplementary-groups.sh \
  build-dry.sh \
  structured-attrs.sh \
  shell.sh \
  brotli.sh \
  zstd.sh \
  compression-levels.sh \
  nix-copy-ssh.sh \
  post-hook.sh \
  function-trace.sh \
  flakes/config.sh \
  fmt.sh \
  eval-store.sh \
  why-depends.sh \
  derivation-json.sh \
  import-derivation.sh \
  nix_path.sh \
  case-hack.sh \
  placeholders.sh \
  ssh-relay.sh \
  plugins.sh \
  build.sh \
  build-delete.sh \
  output-normalization.sh \
  selfref-gc.sh \
  db-migration.sh \
  bash-profile.sh \
  pass-as-file.sh \
  nix-profile.sh \
  suggestions.sh \
  store-ping.sh \
  fetchClosure.sh \
  completions.sh \
  flakes/show.sh \
  impure-derivations.sh \
  path-from-hash-part.sh \
  test-libstoreconsumer.sh \
  toString-path.sh \
  read-only-store.sh

ifeq ($(HAVE_LIBCPUID), 1)
	nix_tests += compute-levels.sh
endif

install-tests += $(foreach x, $(nix_tests), $(d)/$(x))

clean-files += \
  $(d)/common/vars-and-functions.sh \
  $(d)/config.nix

test-deps += \
  tests/common/vars-and-functions.sh \
  tests/config.nix \
  tests/test-libstoreconsumer/test-libstoreconsumer

ifeq ($(BUILD_SHARED_LIBS), 1)
  test-deps += tests/plugins/libplugintest.$(SO_EXT)
endif
