noinst_LIBRARIES = libexpr.a

libexpr_a_SOURCES = nixexpr.cc nixexpr.hh parser.cc parser.hh \
 eval.cc eval.hh primops.cc primops.hh \
 lexer-tab.c lexer-tab.h parser-tab.c parser-tab.h

EXTRA_DIST = lexer.l parser.y

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore
AM_CFLAGS = \
 -I../../externals/inst/include


# Parser generation.

parser.o: parser-tab.h lexer-tab.h

parser-tab.c parser-tab.h: parser.y
	$(bison) -v -o parser-tab.c parser.y -d

lexer-tab.c lexer-tab.h: lexer.l
	$(flex) --outfile lexer-tab.c --header-file=lexer-tab.h lexer.l 


CLEANFILES =
