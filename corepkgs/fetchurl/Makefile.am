all-local: fetchurl.sh

install-exec-local:
	$(INSTALL) -d $(datadir)/fix/fetchurl
	$(INSTALL_DATA) fetchurl.fix $(datadir)/fix/fetchurl
	$(INSTALL_PROGRAM) fetchurl.sh $(datadir)/fix/fetchurl

include ../../substitute.mk

EXTRA_DIST = fetchurl.fix fetchurl.sh.in
