noinst_LIBRARIES = libstore.a

libstore_a_SOURCES = \
 store.cc store.hh storeexpr.cc storeexpr.hh \
 normalise.cc misc.cc normalise.hh \
 globals.cc globals.hh db.cc db.hh \
 references.cc references.hh pathlocks.cc pathlocks.hh \
 gc.cc gc.hh

AM_CXXFLAGS = -Wall \
 -I.. ${bdb_include} ${aterm_include} -I../libutil
