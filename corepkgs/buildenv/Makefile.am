install-exec-local:
	$(INSTALL) -d $(datadir)/nix/corepkgs
	$(INSTALL) -d $(datadir)/nix/corepkgs/buildenv
	$(INSTALL_DATA) default.nix $(datadir)/nix/corepkgs/buildenv
	$(INSTALL_PROGRAM) builder.pl $(datadir)/nix/corepkgs/buildenv

include ../../substitute.mk

EXTRA_DIST = default.nix builder.pl
