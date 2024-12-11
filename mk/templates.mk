template-files :=

# Create the file $(1) from $(1).in by running config.status (which
# substitutes all ‘@var@’ variables set by the configure script).
define instantiate-template

  clean-files += $(1)

endef

ifneq ($(MAKECMDGOALS), clean)

$(buildprefix)%.h: %.h.in $(buildprefix)config.status
	$(trace-gen) rm -f $@ && cd $(buildprefixrel) && ./config.status --quiet --header=$(@:$(buildprefix)%=%)

$(buildprefix)%: %.in $(buildprefix)config.status
	$(trace-gen) rm -f $@ && cd $(buildprefixrel) && ./config.status --quiet --file=$(@:$(buildprefix)%=%)

endif
