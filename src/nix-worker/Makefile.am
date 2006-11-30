libexec_PROGRAMS = nix-worker

nix_worker_SOURCES = main.cc
nix_worker_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} $(aterm_include) -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain
