# Add a rule for creating $(1) as a directory.  This template may be
# called multiple times for the same directory.
define create-dir
  ifndef $(1)_SEEN
    $(1)_SEEN = 1
    $(1):
	$$(trace-mkdir) install -d $(1)
  endif
endef


# Add a rule for installing file $(1) as file $(2) with mode $(3).
# The directory containing $(2) will be created automatically.
define install-file-as

  install: $(2)

  $$(eval $$(call create-dir,$$(dir $(2))))

  $(2): $(1) | $$(dir $(2))
	$$(trace-install) install -m $(3) $(1) $(2)

endef


# Add a rule for installing file $(1) in directory $(2) with mode
# $(3).  The directory will be created automatically.
define install-file-in
  $$(eval $$(call install-file-as,$(1),$(2)/$$(notdir $(1)),$(3)))
endef


define install-program-in
  $$(eval $$(call install-file-in,$(1),$(2),0755))
endef


define install-data-in
  $$(eval $$(call install-file-in,$(1),$(2),0644))
endef


# Install a symlink from $(2) to $(1).  Note that $(1) need not exist.
define install-symlink

  install: $(2)

  $$(eval $$(call create-dir,$$(dir $(2))))

  $(2): | $$(dir $(2))
	$$(trace-install) ln -sfn $(1) $(2)

endef


print-top-help += \
  echo "  install: Install into \$$(prefix) (currently set to '$(prefix)')";
