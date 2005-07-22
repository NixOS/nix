bin_PROGRAMS = nix-instantiate

nix_instantiate_SOURCES = main.cc help.txt
nix_instantiate_LDADD = ../libmain/libmain.la ../libexpr/libexpr.la \
 ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

main.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. ${bdb_include} ${aterm_include} -I../libutil -I../libstore \
 -I../libexpr -I../libmain
