bin_PROGRAMS = nix nix-hash fix
check_PROGRAMS = test

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I..

nix_SOURCES = nix.cc
nix_LDADD = libshared.a libnix.a -ldb_cxx-4 -lATerm

nix_hash_SOURCES = nix-hash.cc
nix_hash_LDADD = libshared.a libnix.a -ldb_cxx-4 -lATerm

fix_SOURCES = fix.cc
fix_LDADD = libshared.a libnix.a -ldb_cxx-4 -lATerm

TESTS = test

test_SOURCES = test.cc
test_LDADD = libshared.a libnix.a -ldb_cxx-4 -lATerm

noinst_LIBRARIES = libnix.a libshared.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c \
 fstate.cc store.cc globals.cc db.cc

libshared_a_SOURCES = shared.cc

libshared_a_CXXFLAGS = \
 -DNIX_STORE_DIR=\"$(prefix)/store\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\"

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/links
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/store
	$(bindir)/nix --init
