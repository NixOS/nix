pkglib_LTLIBRARIES = libexpr.la

libexpr_la_SOURCES = \
 nixexpr.cc eval.cc primops.cc lexer-tab.cc parser-tab.cc \
 get-drvs.cc attr-path.cc expr-to-xml.cc

pkginclude_HEADERS = \
 nixexpr.hh eval.hh parser.hh lexer-tab.hh parser-tab.hh \
 get-drvs.hh attr-path.hh expr-to-xml.hh

libexpr_la_LIBADD = ../libutil/libutil.la ../libstore/libstore.la \
 ../boost/format/libformat.la

BUILT_SOURCES = nixexpr-ast.cc nixexpr-ast.hh \
 parser-tab.hh lexer-tab.hh parser-tab.cc lexer-tab.cc

EXTRA_DIST = lexer.l parser.y nixexpr-ast.def nixexpr-ast.cc

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} ${aterm_include} \
 -I$(srcdir)/../libutil -I$(srcdir)/../libstore
AM_CFLAGS = \
 ${aterm_include}


# Parser generation.

parser-tab.cc parser-tab.hh: parser.y
	$(bison) -v -o parser-tab.cc $(srcdir)/parser.y -d

lexer-tab.cc lexer-tab.hh: lexer.l
	$(flex) --outfile lexer-tab.cc --header-file=lexer-tab.hh $(srcdir)/lexer.l 


# ATerm helper function generation.

nixexpr-ast.cc nixexpr-ast.hh: ../aterm-helper.pl nixexpr-ast.def
	$(perl) $(srcdir)/../aterm-helper.pl nixexpr-ast.hh nixexpr-ast.cc < $(srcdir)/nixexpr-ast.def


CLEANFILES =


# SDF stuff (not built by default).
nix.tbl: nix.sdf
	sdf2table -m Nix -s -i nix.sdf -o nix.tbl

test.ast: test.nix nix.tbl
	sglri -p nix.tbl -i test.nix -o test.ast
