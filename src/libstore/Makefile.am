pkglib_LTLIBRARIES = libstore.la

libstore_la_SOURCES = \
 store.cc store.hh derivations.cc derivations.hh \
 build.cc build.hh \
 misc.cc misc.hh \
 globals.cc globals.hh db.cc db.hh \
 references.cc references.hh pathlocks.cc pathlocks.hh \
 gc.cc gc.hh 

BUILT_SOURCES = derivations-ast.cc derivations-ast.hh

EXTRA_DIST = derivations-ast.def derivations-ast.cc

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. ${bdb_include} ${aterm_include} -I$(srcdir)/../libutil

derivations-ast.cc derivations-ast.hh: ../aterm-helper.pl derivations-ast.def
	$(perl) $(srcdir)/../aterm-helper.pl derivations-ast.hh derivations-ast.cc < $(srcdir)/derivations-ast.def
