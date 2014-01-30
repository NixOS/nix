check:
	@echo "Warning: Nix has no 'make check'. Please install Nix and run 'make installcheck' instead."

nix_tests = \
  init.sh hash.sh lang.sh add.sh simple.sh dependencies.sh \
  parallel.sh build-hook.sh substitutes.sh substitutes2.sh \
  fallback.sh nix-push.sh gc.sh gc-concurrent.sh verify.sh nix-pull.sh \
  referrers.sh user-envs.sh logging.sh nix-build.sh misc.sh fixed.sh \
  gc-runtime.sh install-package.sh check-refs.sh filter-source.sh \
  remote-store.sh export.sh export-graph.sh negative-caching.sh \
  binary-patching.sh timeout.sh secure-drv-outputs.sh nix-channel.sh \
  multiple-outputs.sh import-derivation.sh fetchurl.sh optimise-store.sh \
  binary-cache.sh nix-profile.sh

INSTALL_TESTS += $(foreach x, $(nix_tests), tests/$(x))

TESTS_ENVIRONMENT = NIX_REMOTE= $(bash) -e

clean_files += $(d)/common.sh

installcheck: $(d)/common.sh
