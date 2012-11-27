pkglib_LTLIBRARIES = libmain.la

libmain_la_SOURCES = shared.cc

libmain_la_LIBADD = ../libstore/libstore.la @BDW_GC_LIBS@

pkginclude_HEADERS = shared.hh

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore
