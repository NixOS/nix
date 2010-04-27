bin_PROGRAMS = nix-env

nix_env_SOURCES = nix-env.cc profiles.cc profiles.hh user-env.cc user-env.hh help.txt

nix_env_LDADD = ../libmain/libmain.la ../libexpr/libexpr.la \
 ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la @ADDITIONAL_NETWORK_LIBS@

nix-env.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I$(srcdir)/.. \
 -I$(srcdir)/../libutil -I$(srcdir)/../libstore \
 -I$(srcdir)/../libexpr -I$(srcdir)/../libmain -I../libexpr
