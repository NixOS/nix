all-local: nar.sh unnar.sh

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_DATA) nar.nix $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_PROGRAM) nar.sh $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_DATA) unnar.nix $(DESTDIR)$(datadir)/nix/corepkgs/nar
	$(INSTALL_PROGRAM) unnar.sh $(DESTDIR)$(datadir)/nix/corepkgs/nar

include ../../substitute.mk

EXTRA_DIST = nar.nix nar.sh.in unnar.nix unnar.sh.in
