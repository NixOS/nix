bin_SCRIPTS = nix-collect-garbage \
 nix-pull nix-push nix-prefetch-url \
 nix-install-package

noinst_SCRIPTS = nix-profile.sh

nix-pull nix-push: readmanifest.pm

install-exec-local: readmanifest.pm
	$(INSTALL) -d $(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(libexecdir)/nix
	$(INSTALL_DATA) readmanifest.pm $(libexecdir)/nix 
	$(INSTALL) -d $(sysconfdir)/nix
# !!! don't overwrite local modifications
	$(INSTALL_DATA) prebuilts.conf $(sysconfdir)/nix/prebuilts.conf

include ../substitute.mk

EXTRA_DIST = nix-collect-garbage.in \
 nix-pull.in nix-push.in nix-profile.sh.in \
 nix-prefetch-url.in nix-install-package.in \
 prebuilts.conf readmanifest.pm

