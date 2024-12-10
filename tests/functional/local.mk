nix_tests = \
  test-infra.sh \
  gc.sh \
  nix-collect-garbage-d.sh \
  remote-store.sh \
  legacy-ssh-store.sh \
  lang.sh \
  lang-gc.sh \
  characterisation-test-infra.sh \
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
  hash-convert.sh \
  hash-path.sh \
  gc-non-blocking.sh \
  check.sh \
  nix-shell.sh \
  check-refs.sh \
  build-remote-input-addressed.sh \
  secure-drv-outputs.sh \
  restricted.sh \
  fetchGitSubmodules.sh \
  fetchGitVerification.sh \
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
  build-remote-with-mounted-ssh-ng.sh \
  nar-access.sh \
  impure-eval.sh \
  pure-eval.sh \
  eval.sh \
  repl.sh \
  binary-cache-build-remote.sh \
  search.sh \
  logging.sh \
  export.sh \
  config.sh \
  add.sh \
  chroot-store.sh \
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
  nix-copy-ssh-ng.sh \
  post-hook.sh \
  function-trace.sh \
  fmt.sh \
  eval-store.sh \
  why-depends.sh \
  derivation-json.sh \
  derivation-advanced-attributes.sh \
  import-derivation.sh \
  nix_path.sh \
  nars.sh \
  placeholders.sh \
  ssh-relay.sh \
  build.sh \
  build-delete.sh \
  output-normalization.sh \
  selfref-gc.sh \
  db-migration.sh \
  bash-profile.sh \
  pass-as-file.sh \
  nix-profile.sh \
  suggestions.sh \
  store-info.sh \
  fetchClosure.sh \
  completions.sh \
  impure-derivations.sh \
  path-from-hash-part.sh \
  path-info.sh \
  toString-path.sh \
  read-only-store.sh \
  nested-sandboxing.sh \
  impure-env.sh \
  debugger.sh \
  extra-sandbox-profile.sh \

ifeq ($(HAVE_LIBCPUID), 1)
  nix_tests += compute-levels.sh
endif

ifeq ($(ENABLE_BUILD), yes)
  nix_tests += test-libstoreconsumer.sh

  ifeq ($(BUILD_SHARED_LIBS), 1)
    nix_tests += plugins.sh
  endif
endif

ifeq ($(ENABLE_DOC_GEN), yes)
    nix_tests += help.sh
endif

$(d)/test-libstoreconsumer.sh.test $(d)/test-libstoreconsumer.sh.test-debug: \
  $(buildprefix)$(d)/test-libstoreconsumer/test-libstoreconsumer
$(d)/plugins.sh.test $(d)/plugins.sh.test-debug: \
  $(buildprefix)$(d)/plugins/libplugintest.$(SO_EXT)

install-tests += $(foreach x, $(nix_tests), $(d)/$(x))

test-clean-files := \
  $(d)/common/subst-vars.sh \
  $(d)/config.nix

clean-files += $(test-clean-files)
test-deps += $(test-clean-files)
