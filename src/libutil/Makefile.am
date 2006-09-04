pkglib_LTLIBRARIES = libutil.la

libutil_la_SOURCES = util.cc hash.cc archive.cc aterm.cc aterm-map.cc xml-writer.cc

libutil_la_LIBADD = ../boost/format/libformat.la

pkginclude_HEADERS = util.hh hash.hh archive.hh aterm.hh aterm-map.hh xml-writer.hh types.hh

if !HAVE_OPENSSL
libutil_la_SOURCES += \
 md5.c md5.h sha1.c sha1.h sha256.c sha256.h md32_common.h
endif

AM_CXXFLAGS = -Wall -I$(srcdir)/.. ${aterm_include}
