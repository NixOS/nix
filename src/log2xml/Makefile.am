bin_PROGRAMS = log2xml

log2xml_SOURCES = log2xml.cc

%.xml: %.log log2xml
	./log2xml < $< > $@

%.html: %.xml log2html.xsl
	xsltproc log2html.xsl $< > $@