bin_PROGRAMS = nix-hash

nix_hash_SOURCES = nix-hash.cc
nix_hash_LDADD = ../libmain/libmain.a ../libnix/libnix.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libnix -I../libmain
