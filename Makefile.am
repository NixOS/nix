SUBDIRS = externals src scripts corepkgs doc
EXTRA_DIST = substitute.mk nix.spec nix.spec.in

include ./substitute.mk

nix.spec: nix.spec.in

rpm: nix.spec dist
	rpm $(EXTRA_RPM_FLAGS) -ta $(distdir).tar.gz

relname:
	echo -n $(distdir) > relname

install-data-local: init-state

if INIT_STATE
init-state:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/db
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/nix
	$(INSTALL) -d $(DESTDIR)$(prefix)/store
#	$(bindir)/nix-store --init
else
init-state:
endif