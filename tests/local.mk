check:
	@echo "Warning: Nix has no 'make check'. Please install Nix and run 'make installcheck' instead."

# So they may be commented out individually
nix_tests += hash.sh
nix_tests += lang.sh
nix_tests += add.sh
nix_tests += simple.sh
nix_tests += dependencies.sh
nix_tests += build-hook.sh
nix_tests += gc.sh
nix_tests += gc-concurrent.sh
nix_tests += referrers.sh
nix_tests += user-envs.sh
nix_tests += logging.sh
nix_tests += nix-build.sh
nix_tests += misc.sh
nix_tests += fixed.sh
nix_tests += gc-runtime.sh
nix_tests += check-refs.sh
nix_tests += filter-source.sh
nix_tests += remote-store.sh
nix_tests += export.sh
nix_tests += export-graph.sh
nix_tests += timeout.sh
nix_tests += secure-drv-outputs.sh
nix_tests += nix-channel.sh
nix_tests += multiple-outputs.sh
nix_tests += import-derivation.sh
nix_tests += fetchurl.sh
nix_tests += optimise-store.sh
nix_tests += binary-cache.sh
nix_tests += nix-profile.sh
nix_tests += repair.sh
nix_tests += dump-db.sh
nix_tests += case-hack.sh
nix_tests += check-reqs.sh
nix_tests += pass-as-file.sh
nix_tests += tarball.sh
nix_tests += restricted.sh
nix_tests += placeholders.sh
nix_tests += nix-shell.sh
nix_tests += linux-sandbox.sh
nix_tests += build-remote.sh
nix_tests += nar-index.sh
#nix_tests += parallel.sh

install-tests += $(foreach x, $(nix_tests), tests/$(x))

tests-environment = NIX_REMOTE= PAGER= $(bash) -e
