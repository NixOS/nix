.PHONY: internal-api-html

ifeq ($(internal_api_docs), yes)

$(docdir)/internal-api/html/index.html $(docdir)/internal-api/latex: $(d)/doxygen.cfg
	mkdir -p $(docdir)/internal-api
	{ cat $< ; echo "OUTPUT_DIRECTORY=$(docdir)/internal-api" ; } | doxygen -

# Generate the HTML API docs for Nix's unstable internal interfaces.
internal-api-html: $(docdir)/internal-api/html/index.html

else

# Make a nicer error message
internal-api-html:
	@echo "Internal API docs are disabled. Configure with '--enable-internal-api-docs', or avoid calling 'make internal-api-html'."
	@exit 1

endif
