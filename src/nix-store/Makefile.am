bin_PROGRAMS = nix-store

nix_store_SOURCES = main.cc dotgraph.cc dotgraph.hh help.txt
nix_store_LDADD = ../libmain/libmain.a ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a ${bdb_lib} ${aterm_lib}

main.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. ${bdb_include} $(aterm_include) -I../libutil -I../libstore -I../libmain
