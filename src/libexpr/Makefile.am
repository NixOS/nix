noinst_LIBRARIES = libexpr.a

libexpr_a_SOURCES = nixexpr.cc nixexpr.hh parser.cc parser.hh \
 eval.cc eval.hh primops.cc \
 lexer-tab.c lexer-tab.h parser-tab.c parser-tab.h \
 constructors.hh

EXTRA_DIST = lexer.l parser.y constructors.def constructors.cc \
 aterm-helper.pl

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

constructors.cc constructors.hh: aterm-helper.pl constructors.def
	$(perl) aterm-helper.pl constructors.hh constructors.cc < constructors.def

nixexpr.hh: constructors.hh


CLEANFILES =
