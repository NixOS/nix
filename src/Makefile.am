bin_PROGRAMS = nix nix-hash fix
check_PROGRAMS = test

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I..

nix_SOURCES = nix.cc shared.cc
nix_LDADD = libnix.a -ldb_cxx-4 -lATerm

nix_hash_SOURCES = nix-hash.cc shared.cc
nix_hash_LDADD = libnix.a -ldb_cxx-4 -lATerm

fix_SOURCES = fix.cc shared.cc
fix_LDADD = libnix.a -ldb_cxx-4 -lATerm

TESTS = test

test_SOURCES = test.cc shared.cc
test_LDADD = libnix.a -ldb_cxx-4 -lATerm

noinst_LIBRARIES = libnix.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c \
 fstate.cc store.cc globals.cc db.cc

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/links
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/store
	$(bindir)/nix --init
