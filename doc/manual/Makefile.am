DOCBOOK_DTD = /nix/current/xml/dtd/docbook
DOCBOOK_XSL = /nix/current/xml/xsl/docbook
XML = /usr/share/doc/packages/sp/html-xml/xml.soc

%.is-valid: %.xml
	SP_CHARSET_FIXED=YES SP_ENCODING=XML \
	nsgmls -wxml -c $(XML) -c $(DOCBOOK_DTD)/docbook.cat -ges $<
	touch $@

man1_MANS = nix.1

man nix.1: book.is-valid
	xsltproc $(DOCBOOK_XSL)/manpages/docbook.xsl book.xml

%.html: %.xml %.is-valid
	xsltproc --output book.html $(DOCBOOK_XSL)/html/docbook.xsl book.xml

all-local: book.html

install-data-local: book.html
	$(INSTALL) -d $(datadir)/nix/manual
	$(INSTALL_DATA) book.html $(datadir)/nix/manual
