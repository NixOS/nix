noinst_LIBRARIES = libstore.a

libstore_a_SOURCES = \
 store.cc storeexpr.cc normalise.cc exec.cc \
 globals.cc db.cc references.cc pathlocks.cc

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall \
 -I.. -I../../externals/inst/include -I../libutil

EXTRA_DIST = *.hh *.h test-builder-*.sh test.cc
