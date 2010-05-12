pkglib_LTLIBRARIES = libstore.la

libstore_la_SOURCES = \
  store-api.cc local-store.cc remote-store.cc derivations.cc build.cc misc.cc \
  globals.cc references.cc pathlocks.cc gc.cc \
  optimise-store.cc

pkginclude_HEADERS = \
  store-api.hh local-store.hh remote-store.hh derivations.hh misc.hh \
  globals.hh references.hh pathlocks.hh \
  worker-protocol.hh

libstore_la_LIBADD = ../libutil/libutil.la ../boost/format/libformat.la @ADDITIONAL_NETWORK_LIBS@

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. -I$(srcdir)/../libutil
