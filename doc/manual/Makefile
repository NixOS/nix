DOCBOOK_DTD = /nix/current/xml/dtd/docbook
DOCBOOK_XSL  =/nix/current/xml/xsl/docbook/

check:
	SP_CHARSET_FIXED=YES SP_ENCODING=XML \
	nsgmls -wxml -c /usr/share/sgml/opensp/xml.soc -c $(DOCBOOK_DTD)/docbook.cat -ges book.xml

html:
	mkdir -p out
	xsltproc --output out/book.html $(DOCBOOK_XSL)/html/docbook.xsl book.xml
