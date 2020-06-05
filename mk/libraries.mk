libs-list :=

ifeq ($(OS), Darwin)
  SO_EXT = dylib
else
  ifeq (CYGWIN,$(findstring CYGWIN,$(OS)))
    SO_EXT = dll
  else
    SO_EXT = so
  endif
endif

# Build a library with symbolic name $(1).  The library is defined by
# various variables prefixed by ‘$(1)_’:
#
# - $(1)_NAME: the name of the library (e.g. ‘libfoo’); defaults to
#   $(1).
#
# - $(1)_DIR: the directory where the (non-installed) library will be
#   placed.
#
# - $(1)_SOURCES: the source files of the library.
#
# - $(1)_CFLAGS: additional C compiler flags.
#
# - $(1)_CXXFLAGS: additional C++ compiler flags.
#
# - $(1)_ORDER_AFTER: a set of targets on which the object files of
#   this libraries will have an order-only dependency.
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
# - $(1)_EXCLUDE_FROM_LIBRARY_LIST: if defined, the library will not
#   be automatically marked as a dependency of the top-level all
#   target andwill not be listed in the make help output. This is
#   useful for libraries built solely for testing, for example.
#
# - BUILD_SHARED_LIBS: if equal to ‘1’, a dynamic library will be
#   built, otherwise a static library.
define build-library
  $(1)_NAME ?= $(1)
  _d := $(buildprefix)$$(strip $$($(1)_DIR))
  _srcs := $$(sort $$(foreach src, $$($(1)_SOURCES), $$(src)))
  $(1)_OBJS := $$(addprefix $(buildprefix), $$(addsuffix .o, $$(basename $$(_srcs))))
  _libs := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_PATH))

  ifeq (CYGWIN,$(findstring CYGWIN,$(OS)))
    $(1)_INSTALL_DIR ?= $$(bindir)
  else
    $(1)_INSTALL_DIR ?= $$(libdir)
  endif

  $(1)_LDFLAGS_USE :=
  $(1)_LDFLAGS_USE_INSTALLED :=

  $$(eval $$(call create-dir, $$(_d)))

  ifeq ($(BUILD_SHARED_LIBS), 1)

    ifdef $(1)_ALLOW_UNDEFINED
      ifeq ($(OS), Darwin)
        $(1)_LDFLAGS += -undefined suppress -flat_namespace
      endif
    else
      ifneq ($(OS), Darwin)
        ifneq (CYGWIN,$(findstring CYGWIN,$(OS)))
          $(1)_LDFLAGS += -Wl,-z,defs
        endif
      endif
    endif

    ifneq ($(OS), Darwin)
      $(1)_LDFLAGS += -Wl,-soname=$$($(1)_NAME).$(SO_EXT)
    endif

    $(1)_PATH := $$(_d)/$$($(1)_NAME).$(SO_EXT)

    $$($(1)_PATH): $$($(1)_OBJS) $$(_libs) | $$(_d)/
	$$(trace-ld) $(CXX) -o $$(abspath $$@) -shared $$(LDFLAGS) $$(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$($(1)_LDFLAGS_PROPAGATED) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE)) $$($(1)_LDFLAGS_UNINSTALLED)

    ifneq ($(OS), Darwin)
      $(1)_LDFLAGS_USE += -Wl,-rpath,$$(abspath $$(_d))
    endif
    $(1)_LDFLAGS_USE += -L$$(_d) -l$$(patsubst lib%,%,$$(strip $$($(1)_NAME)))

    $(1)_INSTALL_PATH := $(DESTDIR)$$($(1)_INSTALL_DIR)/$$($(1)_NAME).$(SO_EXT)

    _libs_final := $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_INSTALL_PATH))

    $$(eval $$(call create-dir, $$($(1)_INSTALL_DIR)))

    $$($(1)_INSTALL_PATH): $$($(1)_OBJS) $$(_libs_final) | $(DESTDIR)$$($(1)_INSTALL_DIR)/
	$$(trace-ld) $(CXX) -o $$@ -shared $$(LDFLAGS) $$(GLOBAL_LDFLAGS) $$($(1)_OBJS) $$($(1)_LDFLAGS) $$($(1)_LDFLAGS_PROPAGATED) $$(foreach lib, $$($(1)_LIBS), $$($$(lib)_LDFLAGS_USE_INSTALLED))

    $(1)_LDFLAGS_USE_INSTALLED += -L$$(DESTDIR)$$($(1)_INSTALL_DIR) -l$$(patsubst lib%,%,$$(strip $$($(1)_NAME)))
    ifneq ($(OS), Darwin)
      ifeq ($(SET_RPATH_TO_LIBS), 1)
        $(1)_LDFLAGS_USE_INSTALLED += -Wl,-rpath,$$($(1)_INSTALL_DIR)
      else
        $(1)_LDFLAGS_USE_INSTALLED += -Wl,-rpath-link,$$($(1)_INSTALL_DIR)
      endif
    endif

    ifdef $(1)_FORCE_INSTALL
      install: $$($(1)_INSTALL_PATH)
    endif

  else

    $(1)_PATH := $$(_d)/$$($(1)_NAME).a

    $$($(1)_PATH): $$($(1)_OBJS) | $$(_d)/
	$(trace-ar) $(AR) crs $$@ $$?

    $(1)_LDFLAGS_USE += $$($(1)_PATH) $$($(1)_LDFLAGS)

    $(1)_INSTALL_PATH := $$(libdir)/$$($(1)_NAME).a

  endif

  $(1)_LDFLAGS_USE += $$($(1)_LDFLAGS_PROPAGATED)
  $(1)_LDFLAGS_USE_INSTALLED += $$($(1)_LDFLAGS_PROPAGATED)

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

  ifndef $(1)_EXCLUDE_FROM_LIBRARY_LIST
  libs-list += $$($(1)_PATH)
  endif
  clean-files += $$(_d)/*.a $$(_d)/*.$(SO_EXT) $$(_d)/*.o $$(_d)/.*.dep $$($(1)_DEPS) $$($(1)_OBJS)
  dist-files += $$(_srcs)
endef
