libraries += libexpr

libexpr_NAME = libnixexpr

libexpr_DIR := $(d)
libexpr_RELDIR := $(reldir)

libexpr_SOURCES := $(wildcard $(libexpr_DIR)/*.cc) $(wildcard $(libexpr_DIR)/primops/*.cc) $(libexpr_DIR)/lexer-tab.cc $(libexpr_DIR)/parser-tab.cc

libexpr_LIBS = libutil libstore libformat

libexpr_LDFLAGS =
ifneq ($(OS), FreeBSD)
 libexpr_LDFLAGS += -ldl
endif

# The dependency on libgc must be propagated (i.e. meaning that
# programs/libraries that use libexpr must explicitly pass -lgc),
# because inline functions in libexpr's header files call libgc.
libexpr_LDFLAGS_PROPAGATED = $(BDW_GC_LIBS)

libexpr_ORDER_AFTER := $(addprefix $(libexpr_DIR)/,parser-tab.cc parser-tab.hh lexer-tab.cc lexer-tab.hh)

$(libexpr_DIR)/parser-tab.cc $(libexpr_DIR)/parser-tab.hh: $(libexpr_DIR)/parser.y
	$(trace-gen) cd $(libexpr_DIR) && bison -o parser-tab.cc $< -d

$(libexpr_DIR)/lexer-tab.cc $(libexpr_DIR)/lexer-tab.hh: $(libexpr_DIR)/lexer.l
	$(trace-gen) cd $(libexpr_DIR)/ && flex --outfile lexer-tab.cc --header-file=lexer-tab.hh $<

clean-files += $(addprefix $(libexpr_DIR)/,parser-tab.cc parser-tab.hh lexer-tab.cc lexer-tab.hh)

dist-files += $(addprefix $(libexpr_DIR)/,parser-tab.cc parser-tab.hh lexer-tab.cc lexer-tab.hh)

$(eval $(call install-file-in, $(libexpr_DIR)/nix-expr.pc, $(prefix)/lib/pkgconfig, 0644))
