SUBDIRS = externals src scripts corepkgs doc misc tests
EXTRA_DIST = substitute.mk nix.spec nix.spec.in bootstrap.sh

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
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/profiles
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/gcroots
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/gcroots/tmp
	rm -f $(DESTDIR)$(localstatedir)/nix/gcroots/profiles
	ln -s $(localstatedir)/nix/profiles $(DESTDIR)$(localstatedir)/nix/gcroots/profiles
	$(INSTALL) -d $(DESTDIR)$(prefix)/store
#	$(bindir)/nix-store --init
else
init-state:
endif
