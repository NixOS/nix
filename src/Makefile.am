bin_PROGRAMS = nix # fix
check_PROGRAMS = test

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I..

nix_SOURCES = nix.cc
nix_LDADD = libnix.a -ldb_cxx-4 -lATerm

#fix_SOURCES = fix.cc util.cc hash.cc md5.c
#fix_LDADD = -lATerm

TESTS = test

test_SOURCES = test.cc
test_LDADD = libnix.a -ldb_cxx-4 -lATerm

noinst_LIBRARIES = libnix.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c eval.cc values.cc globals.cc db.cc

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/links
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts/imports
#	$(INSTALL) -d $(localstatedir)/nix/prebuilts/exports
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/values
	$(bindir)/nix --init
