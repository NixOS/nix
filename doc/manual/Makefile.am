ENV = SGML_CATALOG_FILES=$(docbookcatalog)

XMLLINT = $(ENV) $(xmllint) $(xmlflags) --catalogs
XSLTPROC = $(ENV) $(xsltproc) $(xmlflags) --catalogs \
 --param section.autolabel 1 \
 --param section.label.includes.component.label 1

SOURCES = book.xml introduction.xml installation.xml \
 overview.xml \
 nix-store-reference.xml nix-instantiate-reference.xml \
 troubleshooting.xml bugs.xml

book.is-valid: $(SOURCES)
	$(XMLLINT) --noout --valid book.xml
	touch $@

man1_MANS = nix-store.1 nix-instantiate.1

man $(MANS): $(SOURCES) book.is-valid
	$(XSLTPROC) $(docbookxsl)/manpages/docbook.xsl book.xml

book.html: $(SOURCES) book.is-valid
	$(XSLTPROC) --output book.html $(docbookxsl)/html/docbook.xsl book.xml

all-local: book.html

install-data-local: book.html
	$(INSTALL) -d $(datadir)/nix/manual
	$(INSTALL_DATA) book.html $(datadir)/nix/manual

EXTRA_DIST = $(SOURCES) book.html book.is-valid $(MANS) 

DISTCLEANFILES = book.html book.is-valid $(MANS)
