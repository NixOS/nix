.PHONY: external-api-html

ifeq ($(internal_api_docs), yes)

$(docdir)/external-api/html/index.html $(docdir)/external-api/latex: $(d)/doxygen.cfg
	mkdir -p $(docdir)/external-api
	{ cat $< ; echo "OUTPUT_DIRECTORY=$(docdir)/external-api" ; } | doxygen -

# Generate the HTML API docs for Nix's unstable internal interfaces.
external-api-html: $(docdir)/external-api/html/index.html

else

# Make a nicer error message
external-api-html:
	@echo "Internal API docs are disabled. Configure with '--enable-external-api-docs', or avoid calling 'make external-api-html'."
	@exit 1

endif
