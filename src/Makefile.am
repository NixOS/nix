bin_PROGRAMS = nix # fix
noinst_PROGRAMS = test

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I..

nix_SOURCES = nix.cc util.cc hash.cc md5.c eval.cc values.cc globals.cc db.cc
nix_LDADD = -ldb_cxx-4 -lATerm

#fix_SOURCES = fix.cc util.cc hash.cc md5.c
#fix_LDADD = -lATerm

test_SOURCES = test.cc util.cc hash.cc md5.c eval.cc values.cc globals.cc db.cc
test_LDADD = -ldb_cxx-4 -lATerm

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/links
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts/imports
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts/exports
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/values
	$(bindir)/nix --init
