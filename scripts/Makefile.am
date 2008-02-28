bin_SCRIPTS = nix-collect-garbage \
  nix-pull nix-push nix-prefetch-url \
  nix-install-package nix-channel nix-build \
  nix-pack-closure nix-unpack-closure \
  nix-copy-closure 

noinst_SCRIPTS = nix-profile.sh generate-patches.pl \
  find-runtime-roots.pl build-remote.pl nix-reduce-build

nix-pull nix-push: readmanifest.pm readconfig.pm download-using-manifests.pl

install-exec-local: readmanifest.pm download-using-manifests.pl find-runtime-roots.pl
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/profile.d
	$(INSTALL_PROGRAM) nix-profile.sh $(DESTDIR)$(sysconfdir)/profile.d/nix.sh
	$(INSTALL) -d $(DESTDIR)$(libexecdir)/nix
	$(INSTALL_DATA) readmanifest.pm $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_DATA) readconfig.pm $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_PROGRAM) download-using-manifests.pl $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_PROGRAM) find-runtime-roots.pl $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_PROGRAM) generate-patches.pl $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL_PROGRAM) build-remote.pl $(DESTDIR)$(libexecdir)/nix 
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/nix

include ../substitute.mk

EXTRA_DIST = nix-collect-garbage.in \
  nix-pull.in nix-push.in nix-profile.sh.in \
  nix-prefetch-url.in nix-install-package.in \
  nix-channel.in \
  readmanifest.pm.in \
  readconfig.pm.in \
  nix-build.in \
  download-using-manifests.pl.in \
  generate-patches.pl.in \
  nix-pack-closure.in nix-unpack-closure.in \
  nix-copy-closure.in \
  find-runtime-roots.pl.in \
  build-remote.pl.in \
  nix-reduce-build.in
