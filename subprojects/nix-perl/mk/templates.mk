template-files :=

# Create the file $(1) from $(1).in by running config.status (which
# substitutes all ‘@var@’ variables set by the configure script).
define instantiate-template

  clean-files += $(1)

endef

ifneq ($(MAKECMDGOALS), clean)

%.h: %.h.in
	$(trace-gen) rm -f $@ && ./config.status --quiet --header=$@

%: %.in
	$(trace-gen) rm -f $@ && ./config.status --quiet --file=$@

endif
