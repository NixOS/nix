all-local: builder.sh

install-exec-local:
	$(INSTALL) -d $(datadir)/nix/corepkgs
	$(INSTALL) -d $(datadir)/nix/corepkgs/fetchurl
	$(INSTALL_DATA) default.nix $(datadir)/nix/corepkgs/fetchurl
	$(INSTALL_PROGRAM) builder.sh $(datadir)/nix/corepkgs/fetchurl

include ../../substitute.mk

EXTRA_DIST = default.nix builder.sh.in
