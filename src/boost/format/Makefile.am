pkglib_LTLIBRARIES = libformat.la

libformat_la_SOURCES = format_implementation.cc free_funcs.cc \
 parsing.cc 

pkginclude_HEADERS = exceptions.hpp feed_args.hpp format_class.hpp \
 format_fwd.hpp group.hpp internals.hpp internals_fwd.hpp \
 macros_default.hpp

pkgincludedir = ${includedir}/nix/boost/format

AM_CXXFLAGS = -Wall -I$(srcdir)/../..
