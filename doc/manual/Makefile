DOCBOOK_DTD = /nix/current/xml/dtd/docbook
DOCBOOK_XSL = /nix/current/xml/xsl/docbook

all: check html

check:
	SP_CHARSET_FIXED=YES SP_ENCODING=XML \
	nsgmls -wxml -c /usr/share/doc/packages/sp/html-xml/xml.soc -c $(DOCBOOK_DTD)/docbook.cat -ges book.xml

html:
	xsltproc --output book.html $(DOCBOOK_XSL)/html/docbook.xsl book.xml
