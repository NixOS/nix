all-local: nar.sh unnar.sh

install-exec-local:
	$(INSTALL) -d $(datadir)/fix/nar
	$(INSTALL_DATA) nar.fix $(datadir)/fix/nar
	$(INSTALL_DATA) nar.sh $(datadir)/fix/nar
	$(INSTALL_DATA) unnar.fix $(datadir)/fix/nar
	$(INSTALL_DATA) unnar.sh $(datadir)/fix/nar

include ../../substitute.mk

EXTRA_DIST = nar.fix nar.sh unnar.fix unnar.sh
