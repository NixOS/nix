all-local: nar.sh

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_DATA) $(srcdir)/nar.nix $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_PROGRAM) nar.sh $(DESTDIR)$(datadir)/nix/corepkgs/nar

include ../../substitute.mk

EXTRA_DIST = nar.nix nar.sh.in
