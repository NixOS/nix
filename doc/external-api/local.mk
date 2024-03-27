$(docdir)/external-api/html/index.html $(docdir)/external-api/latex: $(d)/doxygen.cfg
	mkdir -p $(docdir)/external-api
	{ cat $< ; echo "OUTPUT_DIRECTORY=$(docdir)/external-api" ; } | doxygen -

# Generate the HTML API docs for Nix's unstable C bindings
.PHONY: external-api-html
external-api-html: $(docdir)/external-api/html/index.html
