bin_PROGRAMS = nix-env

nix_env_SOURCES = nix-env.cc names.cc names.hh \
 profiles.cc profiles.hh help.txt
nix_env_LDADD = ../libmain/libmain.la ../libexpr/libexpr.la \
 ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

nix-env.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} ${aterm_include} \
 -I$(srcdir)/../libutil -I$(srcdir)/../libstore \
 -I$(srcdir)/../libexpr -I$(srcdir)/../libmain -I../libexpr
