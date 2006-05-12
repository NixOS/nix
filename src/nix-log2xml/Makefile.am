bin_PROGRAMS = nix-log2xml

nix_log2xml_SOURCES = log2xml.cc

%.xml: %.log nix-log2xml
	./nix-log2xml < $< > $@

%.html: %.xml mark-errors.xsl log2html.xsl
	$(xsltproc) mark-errors.xsl $< | $(xsltproc) log2html.xsl - > $@

LOG2HTML = $(srcdir)/mark-errors.xsl $(srcdir)/log2html.xsl $(srcdir)/treebits.js

EXTRA_DIST = $(LOG2HTML)

install-data-local:
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/log2html
	$(INSTALL_DATA) $(LOG2HTML) $(DESTDIR)$(datadir)/nix/log2html
