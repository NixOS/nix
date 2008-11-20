SUBDIRS = externals src scripts corepkgs doc misc tests
EXTRA_DIST = substitute.mk nix.spec nix.spec.in bootstrap.sh \
  svn-revision nix.conf.example NEWS

include ./substitute.mk

nix.spec: nix.spec.in

rpm: nix.spec dist
	rpm $(EXTRA_RPM_FLAGS) -ta $(distdir).tar.gz

relname:
	echo -n $(distdir) > relname

install-data-local: init-state
	$(INSTALL) -d $(DESTDIR)$(sysconfdir)/nix
	$(INSTALL_DATA) $(srcdir)/nix.conf.example $(DESTDIR)$(sysconfdir)/nix
	if ! test -e $(DESTDIR)$(sysconfdir)/nix/nix.conf; then \
		$(INSTALL_DATA) $(srcdir)/nix.conf.example $(DESTDIR)$(sysconfdir)/nix/nix.conf; \
	fi
	$(INSTALL) -d $(DESTDIR)$(docdir)
	$(INSTALL_DATA) README $(DESTDIR)$(docdir)/

if INIT_STATE

# For setuid operation, you can enable the following:
# INIT_FLAGS = -g @NIX_GROUP@ -o @NIX_USER@
# GROUP_WRITABLE = -m 775

init-state:
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix/db
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/log/nix
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/log/nix/drvs
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix/profiles
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix/gcroots
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix/temproots
	$(INSTALL) $(INIT_FLAGS) $(GROUP_WRITABLE) -d $(DESTDIR)$(localstatedir)/nix/gcroots/tmp
	$(INSTALL) $(INIT_FLAGS) $(GROUP_WRITABLE) -d $(DESTDIR)$(localstatedir)/nix/gcroots/channels
	ln -sfn $(localstatedir)/nix/profiles $(DESTDIR)$(localstatedir)/nix/gcroots/profiles
	$(INSTALL) $(INIT_FLAGS) -d $(DESTDIR)$(localstatedir)/nix/userpool
	$(INSTALL) $(INIT_FLAGS) -m 1777 -d $(DESTDIR)$(storedir)
	$(INSTALL) $(INIT_FLAGS) $(GROUP_WRITABLE) -d $(DESTDIR)$(localstatedir)/nix/manifests
	ln -sfn $(localstatedir)/nix/manifests $(DESTDIR)$(localstatedir)/nix/gcroots/manifests

else
init-state:
endif

svn-revision:
	svnversion . > svn-revision

all-local: NEWS

NEWS: doc/manual/NEWS.txt
	cp $(srcdir)/doc/manual/NEWS.txt NEWS
