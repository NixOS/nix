bin_SCRIPTS = nix-collect-garbage \
  nix-pull nix-push nix-prefetch-url \
  nix-install-package nix-channel nix-build

noinst_SCRIPTS = nix-profile.sh generate-patches.pl

nix-pull nix-push: readmanifest.pm download-using-manifests.pl

install-exec-local: readmanifest.pm download-using-manifests.pl
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(DESTDIR)$(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(DESTDIR)$(libexecdir)/nix
	$(INSTALL_DATA) readmanifest.pm $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_PROGRAM) download-using-manifests.pl $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/nix

include ../substitute.mk

EXTRA_DIST = nix-collect-garbage.in \
  nix-pull.in nix-push.in nix-profile.sh.in \
  nix-prefetch-url.in nix-install-package.in \
  nix-channel.in \
  readmanifest.pm.in \
  nix-build.in \
  download-using-manifests.pl.in \
  generate-patches.in
