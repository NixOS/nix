bin_PROGRAMS = nix-hash

nix_hash_SOURCES = nix-hash.cc help.txt
nix_hash_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

nix-hash.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore -I../libmain
