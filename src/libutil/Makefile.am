pkglib_LTLIBRARIES = libutil.la

libutil_la_SOURCES = util.cc util.hh hash.cc hash.hh \
 archive.cc archive.hh aterm.cc aterm.hh \
 aterm-map.cc aterm-map.hh \
 xml-writer.cc xml-writer.hh \
 types.hh

if !HAVE_OPENSSL
libutil_la_SOURCES += \
 md5.c md5.h sha1.c sha1.h sha256.c sha256.h md32_common.h
endif

AM_CXXFLAGS = -Wall -I$(srcdir)/.. ${aterm_include}
