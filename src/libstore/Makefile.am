noinst_LIBRARIES = libstore.a

libstore_a_SOURCES = \
 store.cc store.hh derivations.cc derivations.hh \
 build.cc misc.cc build.hh \
 globals.cc globals.hh db.cc db.hh \
 references.cc references.hh pathlocks.cc pathlocks.hh \
 gc.cc gc.hh derivations-ast.hh

EXTRA_DIST = derivations-ast.def derivations-ast.cc

AM_CXXFLAGS = -Wall \
 -I.. ${bdb_include} ${aterm_include} -I../libutil

derivations-ast.cc derivations-ast.hh: ../aterm-helper.pl derivations-ast.def
	$(perl) ../aterm-helper.pl derivations-ast.hh derivations-ast.cc < derivations-ast.def

derivations.cc: derivations-ast.hh