libraries += libexpr

libexpr_NAME = libnixexpr

libexpr_DIR := $(d)

# Workaround for
#   error: creating directory '/nix/var': Permission denied
# We are using nix itself to generate codes
# but it might not be able to run inside sandboxes (see comment above)
nixcmd = env NIX_LOCALSTATE_DIR=$(TMPDIR) \
             NIX_STORE_DIR=$(TMPDIR) \
             NIX_STATE_DIR=$(TMPDIR) \
             NIX_LOG_DIR=$(TMPDIR) \
             NIX_CONF_DIR=$(TMPDIR) nix


libexpr_SOURCES := \
  $(wildcard $(d)/*.cc) \
  $(wildcard $(d)/value/*.cc) \
  $(wildcard $(d)/primops/*.cc) \
  $(wildcard $(d)/flake/*.cc) \
  $(d)/lexer-tab.cc \
  $(d)/parser-tab.cc

libexpr_CXXFLAGS += -I src/libutil -I src/libstore -I src/libfetchers -I src/libmain -I src/libexpr

libexpr_LIBS = libutil libstore libfetchers

libexpr_LDFLAGS += -lboost_context -pthread
ifdef HOST_LINUX
 libexpr_LDFLAGS += -ldl
endif

# The dependency on libgc must be propagated (i.e. meaning that
# programs/libraries that use libexpr must explicitly pass -lgc),
# because inline functions in libexpr's header files call libgc.
libexpr_LDFLAGS_PROPAGATED = $(BDW_GC_LIBS)

libexpr_ORDER_AFTER := $(d)/parser-tab.cc \
                       $(d)/parser-tab.hh \
                       $(d)/lexer-tab.cc \
                       $(d)/lexer-tab.hh \
                       $(d)/diagnostics.inc.hh \
                       $(d)/diagnostics-id.inc.hh

$(d)/diagnostics-id.inc.hh: $(d)/diagnostics-gen.nix $(d)/diagnostics.nix
	$(trace-gen) $(nixcmd) --experimental-features "nix-command" eval --raw --file $< idmacros  > $@

$(d)/diagnostics.inc.hh: $(d)/diagnostics-gen.nix $(d)/diagnostics.nix
	$(trace-gen) $(nixcmd) --experimental-features "nix-command" eval --raw --file $< declarations  > $@

$(d)/parser-tab.cc $(d)/parser-tab.hh: $(d)/parser.y $(d)/parser-prologue.cpp $(d)/parser-epilogue.cpp
	$(trace-gen) bison -v -o $(libexpr_DIR)/parser-tab.cc $< -d

$(d)/lexer-tab.cc $(d)/lexer-tab.hh: $(d)/lexer.l $(d)/lexer-prologue.cpp
	$(trace-gen) flex --outfile $(libexpr_DIR)/lexer-tab.cc --header-file=$(libexpr_DIR)/lexer-tab.hh $<

clean-files += $(d)/parser-tab.cc $(d)/parser-tab.hh $(d)/lexer-tab.cc $(d)/lexer-tab.hh

$(eval $(call install-file-in, $(d)/nix-expr.pc, $(libdir)/pkgconfig, 0644))

$(foreach i, $(wildcard src/libexpr/value/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/value, 0644)))
$(foreach i, $(wildcard src/libexpr/flake/*.hh), \
  $(eval $(call install-file-in, $(i), $(includedir)/nix/flake, 0644)))

$(d)/primops.cc: $(d)/imported-drv-to-derivation.nix.gen.hh $(d)/primops/derivation.nix.gen.hh $(d)/fetchurl.nix.gen.hh

$(d)/flake/flake.cc: $(d)/flake/call-flake.nix.gen.hh

src/libexpr/primops/fromTOML.o:	ERROR_SWITCH_ENUM =
