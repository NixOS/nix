noinst_LIBRARIES = libnix.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c \
 store.cc expr.cc normalise.cc exec.cc \
 globals.cc db.cc references.cc pathlocks.cc

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I.. -I../../externals/inst/include

EXTRA_DIST = *.hh *.h test-builder-*.sh
