bin_PROGRAMS = nix-store

nix_store_SOURCES = nix-store.cc dotgraph.cc dotgraph.hh help.txt
nix_store_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib} @ADDITIONAL_NETWORK_LIBS@

nix-store.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} $(aterm_include) -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain
