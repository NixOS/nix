libraries += libexpr

libexpr_NAME = libnixexpr

libexpr_DIR := $(d)

libexpr_SOURCES := $(wildcard $(d)/*.cc) $(d)/lexer-tab.cc $(d)/parser-tab.cc

libexpr_LIBS = libutil libstore libformat

# The dependency on libgc must be propagated (i.e. meaning that
# programs/libraries that use libexpr must explicitly pass -lgc),
# because inline functions in libexpr's header files call libgc.
libexpr_LDFLAGS_PROPAGATED = $(BDW_GC_LIBS)

$(d)/parser-tab.cc $(d)/parser-tab.hh: $(d)/parser.y
	bison -v -o $(libexpr_DIR)/parser-tab.cc $< -d

$(d)/lexer-tab.cc $(d)/lexer-tab.hh: $(d)/lexer.l
	flex --outfile $(libexpr_DIR)/lexer-tab.cc --header-file=$(libexpr_DIR)/lexer-tab.hh $<
