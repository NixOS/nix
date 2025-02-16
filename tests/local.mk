check:
	@echo "Warning: Nix has no 'make check'. Please install Nix and run 'make installcheck' instead."

# whether to run the tests that assume that we have a local build of
# Nix
HAVE_LOCAL_NIX_BUILD ?= 1

nix_tests = \
  init.sh hash.sh lang.sh add.sh simple.sh dependencies.sh \
  gc.sh \
  gc-concurrent.sh \
  gc-auto.sh \
  referrers.sh user-envs.sh logging.sh nix-build.sh misc.sh fixed.sh \
  gc-runtime.sh check-refs.sh filter-source.sh \
  remote-store.sh export.sh export-graph.sh \
  timeout.sh secure-drv-outputs.sh nix-channel.sh \
  multiple-outputs.sh import-derivation.sh fetchurl.sh optimise-store.sh \
  binary-cache.sh nix-profile.sh repair.sh dump-db.sh case-hack.sh \
  substitute-with-invalid-ca.sh \
  check-reqs.sh pass-as-file.sh tarball.sh restricted.sh \
  placeholders.sh nix-shell.sh \
  linux-sandbox.sh \
  build-dry.sh \
  build-remote.sh \
  nar-access.sh \
  structured-attrs.sh \
  fetchGit.sh \
  fetchMercurial.sh \
  signing.sh \
  run.sh \
  brotli.sh \
  pure-eval.sh \
  check.sh \
  search.sh \
  nix-copy-ssh.sh \
  post-hook.sh \
  function-trace.sh \
  test-infra.sh \
  # parallel.sh

ifeq ($(HAVE_LOCAL_NIX_BUILD), 1)
  nix_tests += plugins.sh
endif

tests/plugins.sh.test: tests/plugins/libplugintest.$(SO_EXT)

install-tests += $(foreach x, $(nix_tests), tests/$(x))

tests-environment = NIX_REMOTE= $(bash) -e

clean-files += $(d)/common.sh

test-deps += tests/common.sh
