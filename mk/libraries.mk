libs_list :=

# Build a library with symbolic name $(1).  The library is defined by
# various variables prefixed by ‘$(1)_’:
#
# - $(1)_NAME: the name of the library (e.g. ‘libfoo’); defaults to
#   $(1).
#
# - $(1)_DIR: the directory containing the sources of the library, and
#   where the (non-installed) library will be placed.
#
# - $(1)_SOURCES: the source files of the library.
#
# - $(1)_LIBS: the symbolic names of other libraries on which this
#   library depends.
#
# - $(1)_ALLOW_UNDEFINED: if set, the library is allowed to have
#   undefined symbols.  Has no effect for static libraries.
#
# - $(1)_LDFLAGS: additional linker flags.
#
# - $(1)_LDFLAGS_PROPAGATED: additional linker flags, also propagated
#   to the linking of programs/libraries that use this library.
#
# - $(1)_FORCE_INSTALL: if defined, the library will be installed even
#   if it's not needed (i.e. dynamically linked) by a program.
#
# - $(1)_INSTALL_DIR: the directory where the library will be
#   installed.  Defaults to $(libdir).
#
# - BUILD_SHARED_LIBS: if equal to ‘1’, a dynamic library will be
#   built, otherwise a static library.
define build-library =
  $(1)_NAME ?= $(1)
  _d := $$(strip $$($(1)_DIR))
  _srcs := $$(foreach src, $$($(1)_SOURCES), $$(_d)/$$(src))
  $(1)_OBJS := $$(addsuffix .o, $$(basename $$(_srcs)))
  _libs := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_PATH))

  $(1)_INSTALL_DIR ?= $$(libdir)

  $(1)_LDFLAGS_USE :=
  $(1)_LDFLAGS_USE_INSTALLED :=

  ifeq ($(BUILD_SHARED_LIBS), 1)

    ifndef $(1)_ALLOW_UNDEFINED
      $(1)_LDFLAGS += -z defs
    endif

    $(1)_PATH := $$(_d)/$$($(1)_NAME).so

    $$($(1)_PATH): $$($(1)_OBJS) $$(_libs)
	$(QUIET) $(CXX) -o $$@ -shared $(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$($(1)_LDFLAGS_PROPAGATED) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE))

    $(1)_LDFLAGS_USE += -L$$(_d) -Wl,-rpath,$$(abspath $$(_d)) -l$$(patsubst lib%,%,$$(strip $$($(1)_NAME)))

    $(1)_INSTALL_PATH := $$($(1)_INSTALL_DIR)/$$($(1)_NAME).so

    _libs_final := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_INSTALL_PATH))

    $$(eval $$(call create-dir,$$($(1)_INSTALL_DIR)))

    $$($(1)_INSTALL_PATH): $$($(1)_OBJS) $$(_libs_final) | $$($(1)_INSTALL_DIR)
	$(QUIET) $(CXX) -o $$@ -shared $(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$($(1)_LDFLAGS_PROPAGATED) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE_INSTALLED))

    $(1)_LDFLAGS_USE_INSTALLED += -L$$($(1)_INSTALL_DIR) -Wl,-rpath,$$($(1)_INSTALL_DIR) -l$$(patsubst lib%,%,$$(strip $$($(1)_NAME)))

    ifdef $(1)_FORCE_INSTALL
      install: $$($(1)_INSTALL_PATH)
    endif

  else

    $(1)_PATH := $$(_d)/$$($(1)_NAME).a

    $$($(1)_PATH): $$($(1)_OBJS)
	$(QUIET) ar crs $$@ $$?

    $(1)_LDFLAGS_USE += $$($(1)_PATH) $$($(1)_LDFLAGS)

    $(1)_INSTALL_PATH := $$(libdir)/$$($(1)_NAME).a

  endif

  $(1)_LDFLAGS_USE += $$($(1)_LDFLAGS_PROPAGATED)
  $(1)_LDFLAGS_USE_INSTALLED += $$($(1)_LDFLAGS_PROPAGATED)

  # Propagate CXXFLAGS to the individual object files.
  $$(foreach obj, $$($(1)_OBJS), $$(eval $$(obj)_CXXFLAGS=$$($(1)_CXXFLAGS)))

  include $$(wildcard $$(_d)/*.dep)

  libs_list += $$($(1)_PATH)
  clean_files += $$(_d)/*.a $$(_d)/*.so $$(_d)/*.o $$(_d)/*.dep
  dist_files += $$(_srcs)
endef
