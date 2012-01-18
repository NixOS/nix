PERL_MODULES = lib/Nix/Store.pm lib/Nix/Manifest.pm lib/Nix/GeneratePatches.pm lib/Nix/SSH.pm lib/Nix/CopyClosure.pm lib/Nix/Config.pm.in

# Hack required by "make check".
all: $(PERL_MODULES:.in=)
	mkdir -p lib/auto/Nix/Store
	ln -sfn $(abs_builddir)/.libs/libNixStore$(dynlib_suffix) lib/auto/Nix/Store/Store$(dynlib_suffix)

install-exec-local: $(PERL_MODULES:.in=)
	$(INSTALL) -d $(DESTDIR)$(perllibdir)/Nix
	$(INSTALL_DATA) $(PERL_MODULES:.in=) $(DESTDIR)$(perllibdir)/Nix
	$(INSTALL) -d $(DESTDIR)$(perllibdir)/auto/Nix/Store
	ln -sfn $(pkglibdir)/libNixStore$(dynlib_suffix) $(DESTDIR)$(perllibdir)/auto/Nix/Store/Store$(dynlib_suffix)

# Awful hackery to get libtool to build Perl XS bindings.
pkglib_LTLIBRARIES = libNixStore.la

libNixStore_la_SOURCES = lib/Nix/Store.cc

libNixStore_la_LIBADD = $(top_builddir)/src/libstore/libstore.la

AM_CXXFLAGS = \
  -I$(top_srcdir)/src -I$(top_srcdir)/src/libutil -I$(top_srcdir)/src/libstore \
  -I$(shell $(perl) -e 'use Config; print $$Config{archlibexp};')/CORE \
  $(shell $(perl) -e 'use Config; print $$Config{ccflags};')

lib/Nix/Store.cc: lib/Nix/Store.xs
	xsubpp $^ -output $@

EXTRA_DIST = $(PERL_MODULES) lib/Nix/Store.xs

include ../substitute.mk
