bin_PROGRAMS = nix-daemon

nix_daemon_SOURCES = nix-daemon.cc
nix_daemon_LDADD = ../libmain/libmain.la ../libstore/libstore.la ../libutil/libutil.la \
 ../boost/format/libformat.la

AM_CXXFLAGS = \
 -I$(srcdir)/.. -I$(srcdir)/../libutil \
 -I$(srcdir)/../libstore -I$(srcdir)/../libmain

install-exec-local:
	ln -sf nix-daemon $(DESTDIR)$(bindir)/nix-worker
