bin_PROGRAMS = nix nix-hash fix
check_PROGRAMS = test

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I.. -I../externals/inst/include $(CXXFLAGS)
AM_LDFLAGS = -L../externals/inst/lib -ldb_cxx -lATerm $(LDFLAGS)

nix_SOURCES = nix.cc
nix_LDADD = libshared.a libnix.a -ldb_cxx -lATerm

nix_hash_SOURCES = nix-hash.cc
nix_hash_LDADD = libshared.a libnix.a -ldb_cxx -lATerm

fix_SOURCES = fix.cc
fix_LDADD = libshared.a libnix.a -ldb_cxx -lATerm

TESTS = test

test_SOURCES = test.cc
test_LDADD = libshared.a libnix.a -ldb_cxx -lATerm

noinst_LIBRARIES = libnix.a libshared.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c \
 store.cc fstate.cc normalise.cc exec.cc \
 globals.cc db.cc references.cc pathlocks.cc

libshared_a_SOURCES = shared.cc

libshared_a_CXXFLAGS = \
 -DNIX_STORE_DIR=\"$(prefix)/store\" \
 -DNIX_DATA_DIR=\"$(datadir)\" \
 -DNIX_STATE_DIR=\"$(localstatedir)/nix\" \
 -DNIX_LOG_DIR=\"$(localstatedir)/log/nix\" \
 $(AM_CXXFLAGS)

nix.o: nix-help.txt.hh

%.hh: %
	echo -n '"' > $@
	sed 's|\(.*\)|\1\\n\\|' < $< >> $@
	echo '"' >> $@

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/db
	$(INSTALL) -d $(localstatedir)/nix/links
	rm -f $(prefix)/current
	ln -sf $(localstatedir)/nix/links/current $(prefix)/current
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/store
	$(bindir)/nix --init

EXTRA_DIST = *.hh *.h
