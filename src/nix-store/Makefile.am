bin_PROGRAMS = nix-store

nix_store_SOURCES = main.cc dotgraph.cc dotgraph.hh help.txt
nix_store_LDADD = ../libmain/libmain.a ../libstore/libstore.a ../libutil/libutil.a \
 ../boost/format/libformat.a -L../../externals/inst/lib -ldb_cxx -lATerm

main.o: help.txt.hh

%.txt.hh: %.txt
	../bin2c/bin2c helpText < $< > $@ || (rm $@ && exit 1)

AM_CXXFLAGS = \
 -I.. -I../../externals/inst/include -I../libutil -I../libstore -I../libmain

install-data-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/db
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/nix/links
	rm -f $(DESTDIR)$(prefix)/current
	ln -sf $(localstatedir)/nix/links/current $(DESTDIR)$(prefix)/current
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/nix
	$(INSTALL) -d $(DESTDIR)$(prefix)/store
#	$(bindir)/nix-store --init
