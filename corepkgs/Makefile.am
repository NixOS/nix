all-local: config.nix

files = nar.nix buildenv.nix buildenv.pl unpack-channel.nix derivation.nix fetchurl.nix \
	imported-drv-to-derivation.nix dynamic-attrs.nix

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL_DATA) config.nix $(files) $(DESTDIR)$(datadir)/nix/corepkgs

include ../substitute.mk

EXTRA_DIST = config.nix.in $(files)
