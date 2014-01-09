template_files :=

# Create the file $(1) from $(1).in by running config.status (which
# substitutes all ‘@var@’ variables set by the configure script).
define instantiate-template =

  clean_files += $(1)

endef

%: %.in
	$(QUIET) ./config.status --quiet --file $@
