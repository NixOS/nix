bin_PROGRAMS = nix-instantiate

nix_instantiate_SOURCES = nix-instantiate.cc help.txt
nix_instantiate_LDADD = ../libmain/libmain.la ../libexpr/libexpr.la \
 ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

nix-instantiate.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 ${bdb_include} ${aterm_include} \
 -I$(srcdir)/.. -I$(srcdir)/../libutil -I$(srcdir)/../libstore \
 -I$(srcdir)/../libexpr -I$(srcdir)/../libmain -I../libexpr
