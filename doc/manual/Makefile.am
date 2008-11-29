XMLLINT = $(xmllint) $(xmlflags)
XSLTPROC = $(xsltproc) $(xmlflags) \
 --param section.autolabel 1 \
 --param section.label.includes.component.label 1 \
 --param html.stylesheet \'style.css\' \
 --param xref.with.number.and.title 1 \
 --param toc.section.depth 3 \
 --param admon.style \'\' \
 --param callout.graphics.extension \'.gif\'

# Note: we use GIF for now, since the PNGs shipped with Docbook aren't
# transparent.

man1_MANS = nix-env.1 nix-build.1 nix-store.1 nix-instantiate.1 \
 nix-collect-garbage.1 nix-push.1 nix-pull.1 \
 nix-prefetch-url.1 nix-channel.1 \
 nix-install-package.1 nix-hash.1 nix-copy-closure.1

man8_MANS = nix-worker.8

FIGURES = figures/user-environments.png

MANUAL_SRCS = manual.xml introduction.xml installation.xml \
 package-management.xml writing-nix-expressions.xml builtins.xml \
 build-farm.xml \
 $(man1_MANS:.1=.xml) $(man8_MANS:.8=.xml) \
 troubleshooting.xml bugs.xml opt-common.xml opt-common-syn.xml \
 env-common.xml quick-start.xml nix-lang-ref.xml glossary.xml \
 conf-file.xml release-notes.xml \
 style.css images

manual.is-valid: $(MANUAL_SRCS) version.txt
#	$(XMLLINT) --xinclude $< | $(XMLLINT) --noout --nonet --relaxng $(docbookrng)/docbook.rng -
	if test "$(jing)" != "false"; then \
		$(XMLLINT) --xinclude $< | $(jing) $(docbookrng)/docbook.rng /dev/fd/0; \
	else \
		echo "Not validating."; \
	fi
	touch $@

version.txt:
	echo -n $(VERSION) > version.txt

man $(MANS): $(MANUAL_SRCS) manual.is-valid
	$(XSLTPROC) --nonet --xinclude $(docbookxsl)/manpages/docbook.xsl manual.xml

manual.html: $(MANUAL_SRCS) manual.is-valid images
	$(XSLTPROC) --nonet --xinclude --output manual.html \
	  $(docbookxsl)/html/docbook.xsl manual.xml

manual.pdf: $(MANUAL_SRCS) manual.is-valid images
	if test "$(dblatex)" != ""; then \
		$(dblatex) manual.xml; \
	else \
		echo "Please install dblatex and rerun configure."; \
		exit 1; \
	fi


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
	$(INSTALL) -d $(DESTDIR)$(docdir)/manual
	$(INSTALL_DATA) manual.html $(DESTDIR)$(docdir)/manual
	ln -sf manual.html $(DESTDIR)$(docdir)/manual/index.html
	$(INSTALL_DATA) style.css $(DESTDIR)$(docdir)/manual
	cp -r images $(DESTDIR)$(docdir)/manual/images
	$(INSTALL) -d $(DESTDIR)$(docdir)/manual/figures
	$(INSTALL_DATA) $(FIGURES) $(DESTDIR)$(docdir)/manual/figures
	$(INSTALL) -d $(DESTDIR)$(docdir)/release-notes
	$(INSTALL_DATA) NEWS.html $(DESTDIR)$(docdir)/release-notes/index.html
	$(INSTALL_DATA) style.css $(DESTDIR)$(docdir)/release-notes/

images:
	mkdir images
#	cp $(docbookxsl)/images/*.gif images
	mkdir images/callouts
	cp $(docbookxsl)/images/callouts/*.gif images/callouts
	chmod -R +w images

KEEP = manual.html manual.is-valid version.txt $(MANS) NEWS.html NEWS.txt

EXTRA_DIST = $(MANUAL_SRCS) $(FIGURES) $(KEEP)

DISTCLEANFILES = $(KEEP)
