bin_SCRIPTS = nix-generate-regscript nix-switch nix-collect-garbage

install-exec-local:
	$(INSTALL) -d $(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(sysconfdir)/profile.d/nix.sh
