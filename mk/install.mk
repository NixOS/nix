# Add a rule for creating $(1) as a directory.  This template may be
# called multiple times for the same directory.
define create-dir =
  ifndef $(1)_SEEN
    $(1)_SEEN = 1
    $(1):
	install -d $(1)
  endif
endef


# Add a rule for installing file $(2) in directory $(1).  The
# directory will be created automatically.
define install-file-in =

  install:: $(1)/$(notdir $(2))

  $$(eval $$(call create-dir,$(1)))

  $(1)/$(notdir $(2)): $(2) | $(1)
	install -t $(1) $(2)

endef
