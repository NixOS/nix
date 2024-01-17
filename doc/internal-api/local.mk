$(docdir)/internal-api/html/index.html $(docdir)/internal-api/latex: $(d)/doxygen.cfg
	mkdir -p $(docdir)/internal-api
	{ cat $< ; echo "OUTPUT_DIRECTORY=$(docdir)/internal-api" ; } | doxygen -

# Generate the HTML API docs for Nix's unstable internal interfaces.
.PHONY: internal-api-html
internal-api-html: $(docdir)/internal-api/html/index.html
