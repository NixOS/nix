bin_PROGRAMS = nix-hash

nix_hash_SOURCES = nix-hash.cc
nix_hash_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil -I$(srcdir)/../libstore -I$(srcdir)/../libmain
