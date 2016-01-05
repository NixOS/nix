check:
	@echo "Warning: Nix has no 'make check'. Please install Nix and run 'make installcheck' instead."

tests-environment = NIX_REMOTE= $(bash) -e

clean-files += $(d)/common.sh

installcheck: $(d)/common.sh
