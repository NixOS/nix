bin_PROGRAMS = nix-hash

nix_hash_SOURCES = nix-hash.cc
nix_hash_LDADD = ../libmain/libmain.a ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx -lATerm

nix-hash.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore -I../libmain
