libexec_PROGRAMS = nix-setuid-helper

nix_setuid_helper_SOURCES = nix-setuid-helper.cc
nix_setuid_helper_LDADD = ../libutil/libutil.la \
 ../boost/format/libformat.la ${aterm_lib}

AM_CXXFLAGS = \
 -I$(srcdir)/.. $(aterm_include) -I$(srcdir)/../libutil
