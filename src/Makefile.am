bin_PROGRAMS = nix fix

nix_SOURCES = nix.cc md5.c
nix_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall
nix_LDADD = -ldb_cxx-4 -lATerm

fix_SOURCES = fix.cc md5.c
fix_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall
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
