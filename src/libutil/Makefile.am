noinst_LIBRARIES = libutil.a

libutil_a_SOURCES = util.cc util.hh hash.cc hash.hh \
 archive.cc archive.hh md5.c md5.h aterm.cc aterm.hh

AM_CXXFLAGS = -DSYSTEM=\"@system@\" -Wall -I.. -I../../externals/inst/include

check_PROGRAMS = test-aterm

test_aterm_SOURCES = test-aterm.cc
test_aterm_LDADD = libnix.a $(LDADD) ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lATerm

