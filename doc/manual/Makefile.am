ENV = SGML_CATALOG_FILES=$(docbookcatalog)

XMLLINT = $(ENV) $(xmllint) $(xmlflags) --catalogs
XSLTPROC = $(ENV) $(xsltproc) $(xmlflags) --catalogs \
 --param section.autolabel 1 \
 --param section.label.includes.component.label 1 \
 --param html.stylesheet \'style.css\'

SOURCES = manual.xml introduction.xml installation.xml overview.xml \
 nix-env.xml nix-store.xml nix-instantiate.xml \
 troubleshooting.xml bugs.xml opt-verbose.xml \
 style.css images

manual.is-valid: $(SOURCES) version.xml
	$(XMLLINT) --noout --valid manual.xml
	touch $@

version.xml:
	echo -n $(VERSION) > version.xml

man1_MANS = nix-env.1 nix-store.1 nix-instantiate.1

man $(MANS): $(SOURCES) manual.is-valid
	$(XSLTPROC) $(docbookxsl)/manpages/docbook.xsl manual.xml

manual.html: $(SOURCES) manual.is-valid images
	$(XSLTPROC) --output manual.html $(docbookxsl)/html/docbook.xsl manual.xml

all-local: manual.html

install-data-local: manual.html
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/manual
	$(INSTALL_DATA) manual.html $(DESTDIR)$(datadir)/nix/manual
	$(INSTALL_DATA) style.css $(DESTDIR)$(datadir)/nix/manual
	cp -r images $(DESTDIR)$(datadir)/nix/manual/images

images:
	mkdir images
	cp $(docbookxsl)/images/*.png images
	mkdir images/callouts
	cp $(docbookxsl)/images/callouts/*.png images/callouts
	chmod +w -R images

KEEP = manual.html manual.is-valid version.xml $(MANS)

EXTRA_DIST = $(SOURCES) $(KEEP)

DISTCLEANFILES = $(KEEP)
