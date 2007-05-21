pkglib_LTLIBRARIES = libutil.la

libutil_la_SOURCES = util.cc hash.cc serialise.cc \
  archive.cc aterm.cc aterm-map.cc xml-writer.cc

pkginclude_HEADERS = util.hh hash.hh serialise.hh \
  archive.hh aterm.hh aterm-map.hh xml-writer.hh types.hh

libutil_la_LIBADD = ../boost/format/libformat.la 

if !HAVE_OPENSSL
libutil_la_SOURCES += \
 md5.c md5.h sha1.c sha1.h sha256.c sha256.h md32_common.h
endif

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. ${aterm_include}
 