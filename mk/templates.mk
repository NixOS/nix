template-files :=

# Create the file $(1) from $(1).in by running config.status (which
# substitutes all ‘@var@’ variables set by the configure script).
define instantiate-template

  clean-files += $(1)

endef

%: %.in
	$(trace-gen) ./config.status --quiet --file $@
