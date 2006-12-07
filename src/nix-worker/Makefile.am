bin_PROGRAMS = nix-worker

nix_worker_SOURCES = nix-worker.cc help.txt
nix_worker_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la ${bdb_lib} ${aterm_lib}

main.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I$(srcdir)/.. ${bdb_include} $(aterm_include) -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain
