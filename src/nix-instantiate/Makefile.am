bin_PROGRAMS = nix-instantiate

nix_instantiate_SOURCES = nixexpr.cc parser.cc eval.cc primops.cc main.cc
nix_instantiate_LDADD = ../libmain/libmain.a ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx \
 -lsglr -lATB -lconversion -lasfix2 -lmept -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore -I../libmain


# Parse table generation.

parser.o: parse-table.h

parse-table.h: nix.tbl
	../bin2c/bin2c nixParseTable < $< > $@ || (rm $@ && exit 1)

%.tbl: %.sdf
	../../externals/inst/bin/sdf2table -i $< -o $@

CLEANFILES = parse-table.h nix.tbl
