bin_PROGRAMS = fix

fix_SOURCES = fix.cc
fix_LDADD = ../libmain/libmain.a ../libnix/libnix.a ../boost/format/libformat.a \
 -L../../externals/inst/lib -ldb_cxx -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libnix -I../libmain
