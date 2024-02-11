libraries += libexpr

libexpr_NAME = libnixexpr

libexpr_DIR := $(d)

libexpr_SOURCES := \
  $(wildcard $(d)/*.cc) \
  $(wildcard $(d)/value/*.cc) \
  $(wildcard $(d)/parser/*.cc) \
  $(wildcard $(d)/primops/*.cc) \
  $(wildcard $(d)/flake/*.cc)

libexpr_CXXFLAGS += -I src/libutil -I src/libstore -I src/libfetchers -I src/libmain -I src/libexpr \
  -I pegtl/include

libexpr_LIBS = libutil libstore libfetchers

libexpr_LDFLAGS += -lboost_context $(THREAD_LDFLAGS)
ifdef HOST_LINUX
  libexpr_LDFLAGS += -ldl
endif

# The dependency on libgc must be propagated (i.e. meaning that
# programs/libraries that use libexpr must explicitly pass -lgc),
# because inline functions in libexpr's header files call libgc.
libexpr_LDFLAGS_PROPAGATED = $(BDW_GC_LIBS)

$(eval $(call install-file-in, $(buildprefix)$(d)/nix-expr.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard src/libexpr/value/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/value, 0644)))
$(foreach i, $(wildcard src/libexpr/flake/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/flake, 0644)))

$(d)/primops.cc: $(d)/imported-drv-to-derivation.nix.gen.hh

$(d)/eval.cc: $(d)/primops/derivation.nix.gen.hh $(d)/fetchurl.nix.gen.hh $(d)/flake/call-flake.nix.gen.hh

$(buildprefix)src/libexpr/primops/fromTOML.o:	ERROR_SWITCH_ENUM =
