noinst_LIBRARIES = libexpr.a

libexpr_a_SOURCES = nixexpr.cc parser.cc eval.cc primops.cc

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore


# Parse table generation.

parser.o: parse-table.h

parse-table.h: nix.tbl
	../bin2c/bin2c nixParseTable < $< > $@ || (rm $@ && exit 1)

%.tbl: %.sdf
	../../externals/inst/bin/sdf2table -s -i $< -o $@

CLEANFILES = parse-table.h nix.tbl
