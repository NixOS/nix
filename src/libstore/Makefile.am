noinst_LIBRARIES = libstore.a

libstore_a_SOURCES = \
 store.cc expr.cc normalise.cc exec.cc \
 globals.cc db.cc references.cc pathlocks.cc

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall \
 -I.. -I../../externals/inst/include -I../libutil

EXTRA_DIST = *.hh *.h test-builder-*.sh

check_PROGRAMS = test-aterm

test_aterm_SOURCES = test-aterm.cc
test_aterm_LDADD = libstore.a $(LDADD) ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lATerm

