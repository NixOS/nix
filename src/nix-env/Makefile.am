bin_PROGRAMS = nix-env

nix_env_SOURCES = main.cc
nix_env_LDADD = ../libmain/libmain.a ../libexpr/libexpr.a \
 ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx \
 -lsglr -lATB -lconversion -lasfix2 -lmept -lATerm

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore \
 -I../libexpr -I../libmain
