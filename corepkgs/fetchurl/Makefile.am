all-local: fetchurl.sh

install-exec-local:
	$(INSTALL) -d $(datadir)/fix/fetchurl
	$(INSTALL_DATA) fetchurl.fix $(datadir)/fix/fetchurl
	$(INSTALL_DATA) fetchurl.sh $(datadir)/fix/fetchurl

include ../../substitute.mk
