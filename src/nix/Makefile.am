bin_PROGRAMS = nix

nix_SOURCES = nix.cc dotgraph.cc
nix_LDADD = ../libmain/libmain.a ../libnix/libnix.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx -lATerm

nix.o: nix-help.txt.hh

%.hh: %
	echo -n '"' > $@
	sed 's|\(.*\)|\1\\n\\|' < $< >> $@
	echo '"' >> $@

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libnix -I../libmain

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/db
	$(INSTALL) -d $(localstatedir)/nix/links
	rm -f $(prefix)/current
	ln -sf $(localstatedir)/nix/links/current $(prefix)/current
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/store
	$(bindir)/nix --init

EXTRA_DIST = *.hh
