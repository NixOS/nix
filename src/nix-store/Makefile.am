bin_PROGRAMS = nix-store

nix_store_SOURCES = main.cc dotgraph.cc dotgraph.hh help.txt.hh
nix_store_LDADD = ../libmain/libmain.a ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx -lATerm

main.o: help.txt.hh

%.hh: %
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore -I../libmain

install-data-local:
	$(INSTALL) -d $(localstatedir)/nix
	$(INSTALL) -d $(localstatedir)/nix/db
	$(INSTALL) -d $(localstatedir)/nix/links
	rm -f $(prefix)/current
	ln -sf $(localstatedir)/nix/links/current $(prefix)/current
	$(INSTALL) -d $(localstatedir)/log/nix
	$(INSTALL) -d $(prefix)/store
	$(bindir)/nix-store --init
