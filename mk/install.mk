# Add a rule for creating $(1) as a directory.  This template may be
# called multiple times for the same directory.
define create-dir
   _i := $$(call add-trailing-slash, $(DESTDIR)$$(strip $(1)))
  ifndef $$(_i)_SEEN
    $$(_i)_SEEN = 1
    $$(_i):
	$$(trace-mkdir) install -d "$$@"
  endif
endef


# Add a rule for installing file $(1) as file $(2) with mode $(3).
# The directory containing $(2) will be created automatically.
define install-file-as

  _i := $(DESTDIR)$$(strip $(2))

  install: $$(_i)

  $$(_i): $(1) | $$(dir $$(_i))
	$$(trace-install) install -m $(3) $(1) "$$@"

  $$(eval $$(call create-dir, $$(dir $(2))))

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

  _i := $(DESTDIR)$$(strip $(2))

  install: $$(_i)

  $$(_i): | $$(dir $$(_i))
	$$(trace-install) ln -sfn $(1) "$$@"

  $$(eval $$(call create-dir, $$(dir $(2))))

endef


print-top-help += \
  echo "  install: Install into \$$(prefix) (currently set to '$(prefix)')";
