all-local: builder.sh

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs/fetchurl
	$(INSTALL_DATA) default.nix $(DESTDIR)$(datadir)/nix/corepkgs/fetchurl
	$(INSTALL_PROGRAM) builder.sh $(DESTDIR)$(datadir)/nix/corepkgs/fetchurl

include ../../substitute.mk

EXTRA_DIST = default.nix builder.sh.in
