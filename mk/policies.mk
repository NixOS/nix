policies-list :=

define build-policy
  $(1)_NAME ?= $(1)

  _d := $$(strip $$($(1)_DIR))

  $(1)_PATH := $$(_d)/$$($(1)_NAME).pp

  $(1)_FC := $$(_d)/$$($(1)_NAME).fc

  $(1)_TE := $$(_d)/$$($(1)_NAME).te

  $(1)_MOD := $$(_d)/$$($(1)_NAME).mod

  $$($(1)_MOD): $$($(1)_TE)
	$(checkmodule) -M -m -o $$($(1)_MOD) $$($(1)_TE)

  $$($(1)_PATH): $$($(1)_MOD) $$($(1)_FC)
	$(sepackage) -o $$($(1)_PATH) -m $$($(1)_MOD) -f $$($(1)_FC)

  $(1)_INSTALL_DIR ?= $$(policydir)

  $(1)_INSTALL_PATH := $$($(1)_INSTALL_DIR)/$$($(1)_NAME).pp

  $$(eval $$(call install-file-as, $$($(1)_PATH), $$($(1)_INSTALL_PATH), 0644))

  policies-list += $$($(1)_PATH)
  clean-files += $$($(1)_PATH) $$($(1)_MOD)
endef
