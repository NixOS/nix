bin_PROGRAMS = fix-ng

fix_ng_SOURCES = fix-expr.cc parser.cc fix.cc
fix_ng_LDADD = ../libmain/libmain.a ../libnix/libnix.a ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lsglr -lATB -lconversion -lasfix2 -lmept -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libnix -I../libmain


# Parse table generation.

parser.o: parse-table.h

parse-table.h: fix.tbl bin2c
	./bin2c fixParseTable < $< > $@ || (rm $@ && exit 1)

noinst_PROGRAMS = bin2c

bin2c_SOURCES = bin2c.c

%.tbl: %.sdf
	../../externals/inst/bin/sdf2table -i $< -o $@

CLEANFILES = parse-table.h fix.tbl
