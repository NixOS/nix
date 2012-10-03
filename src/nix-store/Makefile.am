bin_PROGRAMS = nix-store

nix_store_SOURCES =			\
  nix-store.cc dotgraph.cc dotgraph.hh	\
  xmlgraph.cc xmlgraph.hh

nix_store_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la -lbz2

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain
