bin_SCRIPTS = nix-switch nix-collect-garbage \
 nix-pull nix-push

install-exec-local:
	$(INSTALL) -d $(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(sysconfdir)/nix
# !!! don't overwrite local modifications
	$(INSTALL_DATA) prebuilts.conf $(sysconfdir)/nix/prebuilts.conf
