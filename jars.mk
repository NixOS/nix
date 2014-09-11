define build-jar

  $(1)_NAME ?= $(1)

  _d := $$(strip $$($(1)_DIR))

  $(1)_PATH := $$(_d)/$$($(1)_NAME).jar

  $(1)_TMPDIR := $$(_d)/.$$($(1)_NAME).jar.tmp

  _jars := $$(foreach jar, $$($(1)_JARS), $$($$(jar)_PATH))

  $$($(1)_PATH): $$($(1)_SOURCES) $$(_jars)
	@rm -rf $$($(1)_TMPDIR)
	@mkdir -p $$($(1)_TMPDIR)
	$$(trace-javac) javac $(GLOBAL_JAVACFLAGS) $$($(1)_JAVACFLAGS) -d $$($(1)_TMPDIR) \
	  $$(foreach fn, $$($(1)_SOURCES), '$$(fn)') \
	  -cp "$$(subst $$(space),,$$(foreach jar,$$($(1)_JARS),$$($$(jar)_PATH):))$$$$CLASSPATH"
	$$(trace-jar) jar cf $$($(1)_PATH) -C $$($(1)_TMPDIR) .
	@rm -rf $$($(1)_TMPDIR)

  $(1)_INSTALL_DIR ?= $$(libdir)/java

  $(1)_INSTALL_PATH := $$($(1)_INSTALL_DIR)/$$($(1)_NAME).jar

  $$(eval $$(call install-file-as, $$($(1)_PATH), $$($(1)_INSTALL_PATH), 0644))

  install: $$($(1)_INSTALL_PATH)

  jars-list += $$($(1)_PATH)

  clean-files += $$($(1)_PATH)

endef
