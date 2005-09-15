ENV = SGML_CATALOG_FILES=$(docbookcatalog)

XMLLINT = $(ENV) $(xmllint) $(xmlflags) --catalogs
XSLTPROC = $(ENV) $(xsltproc) $(xmlflags) --catalogs \
 --param section.autolabel 1 \
 --param section.label.includes.component.label 1 \
 --param html.stylesheet \'style.css\' \
 --param xref.with.number.and.title 1 \
 --param toc.section.depth 3

man1_MANS = nix-env.1 nix-build.1 nix-store.1 nix-instantiate.1 \
 nix-collect-garbage.1 nix-push.1 nix-pull.1 \
 nix-prefetch-url.1 nix-channel.1

FIGURES = figures/user-environments.png

MANUAL_SRCS = manual.xml introduction.xml installation.xml \
 package-management.xml writing-nix-expressions.xml \
 build-farm.xml \
 $(man1_MANS:.1=.xml) \
 troubleshooting.xml bugs.xml opt-common.xml opt-common-syn.xml \
 env-common.xml quick-start.xml nix-lang-ref.xml glossary.xml \
 conf-file.xml \
 style.css images

manual.is-valid: $(MANUAL_SRCS) version.txt
	$(XMLLINT) --xinclude $< | $(XMLLINT) --noout --nonet --valid -
	touch $@

version.txt:
	echo -n $(VERSION) > version.txt

man $(MANS): $(MANUAL_SRCS) manual.is-valid
	$(XSLTPROC) --nonet --xinclude $(docbookxsl)/manpages/docbook.xsl manual.xml

manual.html: $(MANUAL_SRCS) manual.is-valid images
	$(XSLTPROC) --nonet --xinclude --output manual.html \
	  $(docbookxsl)/html/docbook.xsl manual.xml


NEWS_OPTS = \
 --stringparam generate.toc "article nop" \
 --stringparam section.autolabel.max.depth 0 \
 --stringparam header.rule 0

NEWS.html: release-notes.xml
	$(XSLTPROC) --nonet --xinclude --output $@ $(NEWS_OPTS) \
	  $(docbookxsl)/html/docbook.xsl release-notes.xml

NEWS.txt: release-notes.xml
	$(XSLTPROC) --nonet --xinclude quote-literals.xsl release-notes.xml | \
	  $(XSLTPROC) --nonet --output $@.tmp.html $(NEWS_OPTS) \
	  $(docbookxsl)/html/docbook.xsl -
	LANG=en_US $(w3m) -dump $@.tmp.html > $@
	rm $@.tmp.html


all-local: manual.html NEWS.html NEWS.txt

install-data-local: manual.html
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/manual
	$(INSTALL_DATA) manual.html $(DESTDIR)$(datadir)/nix/manual
	$(INSTALL_DATA) style.css $(DESTDIR)$(datadir)/nix/manual
	cp -r images $(DESTDIR)$(datadir)/nix/manual/images
	$(INSTALL) -d $(DESTDIR)$(datadir)/nix/manual/figures
	$(INSTALL_DATA) $(FIGURES) $(DESTDIR)$(datadir)/nix/manual/figures

images:
	mkdir images
	cp $(docbookxsl)/images/*.png images
	mkdir images/callouts
	cp $(docbookxsl)/images/callouts/*.png images/callouts
	chmod +w -R images

KEEP = manual.html manual.is-valid version.txt $(MANS) NEWS.html NEWS.txt

EXTRA_DIST = $(MANUAL_SRCS) $(FIGURES) $(KEEP)

DISTCLEANFILES = $(KEEP)
