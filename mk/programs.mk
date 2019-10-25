programs-list :=

# Build a program with symbolic name $(1).  The program is defined by
# various variables prefixed by ‘$(1)_’:
#
# - $(1)_DIR: the directory where the (non-installed) program will be
#   placed.
#
# - $(1)_SOURCES: the source files of the program.
#
# - $(1)_CFLAGS: additional C compiler flags.
#
# - $(1)_CXXFLAGS: additional C++ compiler flags.
#
# - $(1)_ORDER_AFTER: a set of targets on which the object files of
#   this program will have an order-only dependency.
#
# - $(1)_LIBS: the symbolic names of libraries on which this program
#   depends.
#
# - $(1)_LDFLAGS: additional linker flags.
#
# - $(1)_INSTALL_DIR: the directory where the program will be
#   installed; defaults to $(bindir).
define build-program
  _d := $(buildprefix)$$($(1)_DIR)
  _srcs := $$(sort $$(foreach src, $$($(1)_SOURCES), $$(src)))
  $(1)_OBJS := $$(addprefix $(buildprefix), $$(addsuffix .o, $$(basename $$(_srcs))))
  _libs := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_PATH))
  $(1)_PATH := $$(_d)/$(1)

  $$(eval $$(call create-dir, $$(_d)))

  $$($(1)_PATH): $$($(1)_OBJS) $$(_libs) | $$(_d)/
	$$(trace-ld) $(CXX) -o $$@ $$(LDFLAGS) $$(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE))

  $(1)_INSTALL_DIR ?= $$(bindir)
  $(1)_INSTALL_PATH := $$($(1)_INSTALL_DIR)/$(1)

  $$(eval $$(call create-dir, $$($(1)_INSTALL_DIR)))

  install: $(DESTDIR)$$($(1)_INSTALL_PATH)

  ifeq ($(BUILD_SHARED_LIBS), 1)

    _libs_final := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_INSTALL_PATH))

    $(DESTDIR)$$($(1)_INSTALL_PATH): $$($(1)_OBJS) $$(_libs_final) | $(DESTDIR)$$($(1)_INSTALL_DIR)/
	$$(trace-ld) $(CXX) -o $$@ $$(LDFLAGS) $$(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE_INSTALLED))

  else

    $(DESTDIR)$$($(1)_INSTALL_PATH): $$($(1)_PATH) | $(DESTDIR)$$($(1)_INSTALL_DIR)/
	install -t $(DESTDIR)$$($(1)_INSTALL_DIR) $$<

  endif

  # Propagate CFLAGS and CXXFLAGS to the individual object files.
  $$(foreach obj, $$($(1)_OBJS), $$(eval $$(obj)_CFLAGS=$$($(1)_CFLAGS)))
  $$(foreach obj, $$($(1)_OBJS), $$(eval $$(obj)_CXXFLAGS=$$($(1)_CXXFLAGS)))

  # Make each object file depend on the common dependencies.
  $$(foreach obj, $$($(1)_OBJS), $$(eval $$(obj): $$($(1)_COMMON_DEPS) $$(GLOBAL_COMMON_DEPS)))

  # Make each object file have order-only dependencies on the common
  # order-only dependencies. This includes the order-only dependencies
  # of libraries we're depending on.
  $(1)_ORDER_AFTER_CLOSED = $$($(1)_ORDER_AFTER) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_ORDER_AFTER_CLOSED))

  $$(foreach obj, $$($(1)_OBJS), $$(eval $$(obj): | $$($(1)_ORDER_AFTER_CLOSED) $$(GLOBAL_ORDER_AFTER)))

  # Include .dep files, if they exist.
  $(1)_DEPS := $$(foreach fn, $$($(1)_OBJS), $$(call filename-to-dep, $$(fn)))
  -include $$($(1)_DEPS)

  programs-list += $$($(1)_PATH)
  clean-files += $$($(1)_PATH) $$(_d)/*.o $$(_d)/.*.dep $$($(1)_DEPS) $$($(1)_OBJS)
  dist-files += $$(_srcs)
endef
