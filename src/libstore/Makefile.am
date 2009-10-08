pkglib_LTLIBRARIES = libstore.la

libstore_la_SOURCES = \
  store-api.cc local-store.cc remote-store.cc derivations.cc build.cc misc.cc \
  globals.cc db.cc references.cc pathlocks.cc gc.cc upgrade-schema.cc \
  optimise-store.cc

pkginclude_HEADERS = \
  store-api.hh local-store.hh remote-store.hh derivations.hh misc.hh \
  globals.hh db.hh references.hh pathlocks.hh \
  worker-protocol.hh

libstore_la_LIBADD = ../libutil/libutil.la ../boost/format/libformat.la @ADDITIONAL_NETWORK_LIBS@

BUILT_SOURCES = derivations-ast.cc derivations-ast.hh

EXTRA_DIST = derivations-ast.def derivations-ast.cc

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. ${bdb_include} ${aterm_include} -I$(srcdir)/../libutil

derivations-ast.cc derivations-ast.hh: ../aterm-helper.pl derivations-ast.def
	$(perl) $(srcdir)/../aterm-helper.pl derivations-ast.hh derivations-ast.cc < $(srcdir)/derivations-ast.def
