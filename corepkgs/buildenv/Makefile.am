install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs/buildenv
	$(INSTALL_DATA) default.nix $(DESTDIR)$(datadir)/nix/corepkgs/buildenv
	$(INSTALL_PROGRAM) builder.pl $(DESTDIR)$(datadir)/nix/corepkgs/buildenv

include ../../substitute.mk

EXTRA_DIST = default.nix builder.pl
