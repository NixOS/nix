bin_PROGRAMS = nix-instantiate

nix_instantiate_SOURCES = main.cc help.txt
nix_instantiate_LDADD = ../libmain/libmain.a ../libexpr/libexpr.a \
 ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx \
 -lsglr -lATB -lconversion -lasfix2 -lmept -lATerm

main.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore \
 -I../libexpr -I../libmain
