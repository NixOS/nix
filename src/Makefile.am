bin_PROGRAMS = nix fix

CXXFLAGS = -DSYSTEM=\"@host@\" -Wall

nix_SOURCES = nix.cc db.cc util.cc md5.c
nix_LDADD = -ldb_cxx-4 -lATerm

fix_SOURCES = fix.cc util.cc md5.c
fix_LDADD = -lATerm

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/descriptors
	$(INSTALL) -d $(localstatedir)/nix/sources
	$(INSTALL) -d $(localstatedir)/nix/links
	$(INSTALL) -d $(localstatedir)/nix/prebuilts
	$(INSTALL) -d $(localstatedir)/nix/prebuilts/imports
	$(INSTALL) -d $(localstatedir)/nix/prebuilts/exports
	$(INSTALL) -d $(prefix)/pkg
	$(bindir)/nix init
