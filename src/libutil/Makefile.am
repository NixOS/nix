noinst_LIBRARIES = libutil.a

libutil_a_SOURCES = util.cc util.hh hash.cc hash.hh \
 archive.cc archive.hh md5.c md5.h aterm.cc aterm.hh

AM_CXXFLAGS = -DSYSTEM=\"@system@\" -Wall -I.. ${aterm_include}

check_PROGRAMS = test-aterm

test_aterm_SOURCES = test-aterm.cc
test_aterm_LDADD = ./libutil.a ../boost/format/libformat.a \
 ${aterm_lib}

