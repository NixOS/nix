bin_SCRIPTS = nix-collect-garbage \
  nix-pull nix-push nix-prefetch-url \
  nix-install-package nix-channel nix-build

noinst_SCRIPTS = nix-profile.sh

nix-pull nix-push: readmanifest.pm

install-exec-local: readmanifest.pm
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(DESTDIR)$(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(DESTDIR)$(libexecdir)/nix
	$(INSTALL_DATA) readmanifest.pm $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/nix
# !!! don't overwrite local modifications
	$(INSTALL_DATA) prebuilts.conf $(DESTDIR)$(sysconfdir)/nix/prebuilts.conf

include ../substitute.mk

EXTRA_DIST = nix-collect-garbage.in \
  nix-pull.in nix-push.in nix-profile.sh.in \
  nix-prefetch-url.in nix-install-package.in \
  nix-channel.in \
  prebuilts.conf readmanifest.pm.in \
  nix-build.in
