pkglib_LTLIBRARIES = libutil.la

libutil_la_SOURCES = util.cc hash.cc serialise.cc \
  archive.cc xml-writer.cc affinity.cc cgroups.cc

libutil_la_LIBADD = ../boost/format/libformat.la

pkginclude_HEADERS = util.hh hash.hh serialise.hh \
  archive.hh xml-writer.hh types.hh affinity.hh \
  cgroups.hh

if !HAVE_OPENSSL
libutil_la_SOURCES += \
 md5.c md5.h sha1.c sha1.h sha256.c sha256.h md32_common.h
else
libutil_la_LIBADD += @OPENSSL_LIBS@
endif

AM_CXXFLAGS = -Wall -I$(srcdir)/..
