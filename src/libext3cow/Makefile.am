pkglib_LTLIBRARIES = libext3cow.la

libext3cow_la_SOURCES = epoch2date.c snapshot.cc tt.c ext3cow_tools.h

pkginclude_HEADERS = snapshot.hh

#TODO linux kernel header

libext3cow_la_LIBADD = ../libutil/libutil.la \
                       ../boost/format/libformat.la 

AM_CXXFLAGS = -Wall \
 -I$(srcdir)/.. ${aterm_include} \
 -I$(srcdir)/../libutil

AM_CFLAGS = \
 ${aterm_include}