bin_PROGRAMS = fix-ng

fix_ng_SOURCES = fix.cc
fix_ng_LDADD = ../libmain/libmain.a ../libnix/libnix.a ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libnix -I../libmain
