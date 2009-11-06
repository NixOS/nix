bin_PROGRAMS = nix-hash

nix_hash_SOURCES = nix-hash.cc help.txt
nix_hash_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${aterm_lib} @ADDITIONAL_NETWORK_LIBS@

nix-hash.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil -I$(srcdir)/../libstore -I$(srcdir)/../libmain
