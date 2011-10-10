perlversion := $(shell perl -e 'use Config; print $$Config{version};')
perlarchname := $(shell perl -e 'use Config; print $$Config{archname};')
perllibdir = $(libdir)/perl5/site_perl/$(perlversion)/$(perlarchname)

all: lib/Nix/Config.pm

install-exec-local: lib/Nix/*.pm lib/Nix/Config.pm
	$(INSTALL) -d $(DESTDIR)$(perllibdir)/Nix
	$(INSTALL_DATA) lib/Nix/*.pm $(DESTDIR)$(perllibdir)/Nix
	$(INSTALL) -d $(DESTDIR)$(perllibdir)/auto/Nix/Store
	ln -sfn $(pkglibdir)/libNixStore.so $(DESTDIR)$(perllibdir)/auto/Nix/Store/Store.so

# Awful hackery to get libtool to build Perl XS bindings.
pkglib_LTLIBRARIES = libNixStore.la

libNixStore_la_SOURCES = lib/Nix/Store.cc

libNixStore_la_LIBADD = $(top_srcdir)/src/libstore/libstore.la

AM_CXXFLAGS = \
  -I$(top_srcdir)/src -I$(top_srcdir)/src/libutil -I$(top_srcdir)/src/libstore \
  -I$(shell perl -e 'use Config; print $$Config{archlibexp};')/CORE

lib/Nix/Store.cc: lib/Nix/Store.xs
	xsubpp $^ -output $@

EXTRA_DIST = lib/Nix/Store.pm lib/Nix/Manifest.pm lib/Nix/Config.pm.in lib/Nix/Store.xs

include ../substitute.mk
