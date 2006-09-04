pkglib_LTLIBRARIES = libexpr.la

libexpr_la_SOURCES = nixexpr.cc nixexpr.hh parser.cc parser.hh \
 eval.cc eval.hh primops.cc \
 lexer-tab.c lexer-tab.h parser-tab.c parser-tab.h \
 get-drvs.cc get-drvs.hh \
 attr-path.cc attr-path.hh \
 expr-to-xml.cc expr-to-xml.hh 

BUILT_SOURCES = nixexpr-ast.cc nixexpr-ast.hh \
 parser-tab.h lexer-tab.h parser-tab.c lexer-tab.c

EXTRA_DIST = lexer.l parser.y nixexpr-ast.def nixexpr-ast.cc

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} ${aterm_include} \
 -I$(srcdir)/../libutil -I$(srcdir)/../libstore
AM_CFLAGS = \
 ${aterm_include}


# Parser generation.

parser-tab.c parser-tab.h: parser.y
	$(bison) -v -o parser-tab.c $(srcdir)/parser.y -d

lexer-tab.c lexer-tab.h: lexer.l
	$(flex) --outfile lexer-tab.c --header-file=lexer-tab.h $(srcdir)/lexer.l 


# ATerm helper function generation.

nixexpr-ast.cc nixexpr-ast.hh: ../aterm-helper.pl nixexpr-ast.def
	$(perl) $(srcdir)/../aterm-helper.pl nixexpr-ast.hh nixexpr-ast.cc < $(srcdir)/nixexpr-ast.def


CLEANFILES =


# SDF stuff (not built by default).
nix.tbl: nix.sdf
	sdf2table -m Nix -s -i nix.sdf -o nix.tbl

test.ast: test.nix nix.tbl
	sglri -p nix.tbl -i test.nix -o test.ast
