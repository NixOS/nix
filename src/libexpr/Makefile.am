noinst_LIBRARIES = libexpr.a

libexpr_a_SOURCES = nixexpr.cc nixexpr.hh parser.cc parser.hh \
 eval.cc eval.hh primops.cc \
 lexer-tab.c lexer-tab.h parser-tab.c parser-tab.h \
 nixexpr-ast.hh

EXTRA_DIST = lexer.l parser.y nixexpr-ast.def nixexpr-ast.cc

AM_CXXFLAGS = \
 -I.. ${bdb_include} ${aterm_include} -I../libutil -I../libstore
AM_CFLAGS = \
 ${aterm_include}


# Parser generation.

parser.o: parser-tab.h lexer-tab.h

parser-tab.c parser-tab.h: parser.y
	$(bison) -v -o parser-tab.c parser.y -d

lexer-tab.c lexer-tab.h: lexer.l
	$(flex) --outfile lexer-tab.c --header-file=lexer-tab.h lexer.l 


# ATerm helper function generation.

nixexpr-ast.cc nixexpr-ast.hh: ../aterm-helper.pl nixexpr-ast.def
	$(perl) ../aterm-helper.pl nixexpr-ast.hh nixexpr-ast.cc < nixexpr-ast.def

nixexpr.cc nixexpr.hh: nixexpr-ast.hh


CLEANFILES =
