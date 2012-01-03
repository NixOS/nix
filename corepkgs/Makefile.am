SUBDIRS = channels

all-local: config.nix

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/corepkgs
	$(INSTALL_DATA) config.nix $(srcdir)/nar.nix $(srcdir)/buildenv.nix $(srcdir)/buildenv.pl $(DESTDIR)$(datadir)/nix/corepkgs

include ../substitute.mk

EXTRA_DIST = config.nix.in nar.nix buildenv.nix buildenv.pl
