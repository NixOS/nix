noinst_LIBRARIES = libnix.a

libnix_a_SOURCES = util.cc hash.cc archive.cc md5.c \
 store.cc expr.cc normalise.cc exec.cc \
 globals.cc db.cc references.cc pathlocks.cc aterm.cc

AM_CXXFLAGS = -DSYSTEM=\"@host@\" -Wall -I.. -I../../externals/inst/include

EXTRA_DIST = *.hh *.h test-builder-*.sh

check_PROGRAMS = test-aterm

test_aterm_SOURCES = test-aterm.cc
test_aterm_LDADD = libnix.a $(LDADD) ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lATerm

