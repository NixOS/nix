PROGRAMS += nix-log2xml

nix-log2xml_DIR := $(d)

nix-log2xml_SOURCES := $(d)/log2xml.cc

$(foreach file, mark-errors.xsl log2html.xsl treebits.js, \
  $(eval $(call install-data-in, $(d)/$(file), $(datadir)/nix/log2html)))
