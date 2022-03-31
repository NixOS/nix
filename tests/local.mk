nix_tests = \
  flakes.sh \
  ca/gc.sh \
  gc.sh \
  remote-store.sh \
  lang.sh \
  fetchMercurial.sh \
  gc-auto.sh \
  user-envs.sh \
  binary-cache.sh \
  multiple-outputs.sh \
  ca/build.sh \
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
  simple.sh \
  referrers.sh \
  optimise-store.sh \
  substitute-with-invalid-ca.sh \
  ca/concurrent-builds.sh \
  signing.sh \
  ca/build-with-garbage-path.sh \
  hash.sh \
  gc-non-blocking.sh \
  check.sh \
  ca/substitute.sh \
  nix-shell.sh \
  ca/signatures.sh \
  ca/nix-shell.sh \
  ca/nix-copy.sh \
  check-refs.sh \
  build-remote-input-addressed.sh \
  secure-drv-outputs.sh \
  restricted.sh \
  fetchGitSubmodules.sh \
  flake-searching.sh \
  ca/duplicate-realisation-in-closure.sh \
  readfile-context.sh \
  nix-channel.sh \
  recursive.sh \
  dependencies.sh \
  check-reqs.sh \
  build-remote-content-addressed-fixed.sh \
  build-remote-content-addressed-floating.sh \
  nar-access.sh \
  pure-eval.sh \
  eval.sh \
  ca/post-hook.sh \
  repl.sh \
  ca/repl.sh \
  ca/recursive.sh \
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
  build-dry.sh \
  structured-attrs.sh \
  shell.sh \
  brotli.sh \
  zstd.sh \
  compression-levels.sh \
  nix-copy-ssh.sh \
  post-hook.sh \
  function-trace.sh \
  flake-local-settings.sh \
  eval-store.sh \
  why-depends.sh \
  import-derivation.sh \
  ca/import-derivation.sh \
  nix_path.sh \
  case-hack.sh \
  placeholders.sh \
  ssh-relay.sh \
  plugins.sh \
  build.sh \
  ca/nix-run.sh \
  db-migration.sh \
  bash-profile.sh \
  pass-as-file.sh \
  describe-stores.sh \
  nix-profile.sh \
  suggestions.sh \
  store-ping.sh \
  fetchClosure.sh \
  impure-derivations.sh

ifeq ($(HAVE_LIBCPUID), 1)
	nix_tests += compute-levels.sh
endif

install-tests += $(foreach x, $(nix_tests), tests/$(x))

tests-environment = NIX_REMOTE= $(bash) -e

clean-files += $(d)/common.sh $(d)/config.nix $(d)/ca/config.nix

test-deps += tests/common.sh tests/config.nix tests/ca/config.nix tests/plugins/libplugintest.$(SO_EXT)
