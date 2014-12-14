define build-jar

  $(1)_NAME ?= $(1)

  _d := $$(strip $$($(1)_DIR))

  $(1)_PATH := $$(_d)/$$($(1)_NAME).jar

  $(1)_TMPDIR := $$(_d)/.$$($(1)_NAME).jar.tmp

  _jars := $$(foreach jar, $$($(1)_JARS), $$($$(jar)_PATH))

  $$($(1)_PATH): $$($(1)_SOURCES) $$(_jars) $$($(1)_EXTRA_DEPS)| $$($(1)_ORDER_AFTER)
	@rm -rf $$($(1)_TMPDIR)
	@mkdir -p $$($(1)_TMPDIR)
	$$(trace-javac) javac $(GLOBAL_JAVACFLAGS) $$($(1)_JAVACFLAGS) -d $$($(1)_TMPDIR) \
	  $$(foreach fn, $$($(1)_SOURCES), '$$(fn)') \
	  -cp "$$(subst $$(space),,$$(foreach jar,$$($(1)_JARS),$$($$(jar)_PATH):))$$$$CLASSPATH"
	@echo -e '$$(subst $$(newline),\n,$$($(1)_MANIFEST))' > $$($(1)_PATH).manifest
	$$(trace-jar) jar cfm $$($(1)_PATH) $$($(1)_PATH).manifest -C $$($(1)_TMPDIR) .
	@rm $$($(1)_PATH).manifest
	@rm -rf $$($(1)_TMPDIR)

  $(1)_INSTALL_DIR ?= $$(jardir)

  $(1)_INSTALL_PATH := $$($(1)_INSTALL_DIR)/$$($(1)_NAME).jar

  $$(eval $$(call install-file-as, $$($(1)_PATH), $$($(1)_INSTALL_PATH), 0644))

  install: $$($(1)_INSTALL_PATH)

  jars-list += $$($(1)_PATH)

  clean-files += $$($(1)_PATH)

endef
