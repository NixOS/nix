bin_PROGRAMS = nix-worker

nix_worker_SOURCES = nix-worker.cc
nix_worker_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain
