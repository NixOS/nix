all-local: nar.sh unnar.sh

install-exec-local:
	$(INSTALL) -d $(datadir)/nix/corepkgs
	$(INSTALL) -d $(datadir)/nix/corepkgs/nar
	$(INSTALL_DATA) nar.nix $(datadir)/nix/corepkgs/nar
	$(INSTALL_PROGRAM) nar.sh $(datadir)/nix/corepkgs/nar
	$(INSTALL_DATA) unnar.nix $(datadir)/nix/corepkgs/nar
	$(INSTALL_PROGRAM) unnar.sh $(datadir)/nix/corepkgs/nar

include ../../substitute.mk

EXTRA_DIST = nar.nix nar.sh.in unnar.nix unnar.sh.in
