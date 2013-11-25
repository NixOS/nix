default: all


# Include Autoconf variables.
Makefile.config: Makefile.config.in
	./config.status --file $@

include Makefile.config


# Initialise some variables.
QUIET = @


# Pass -fPIC if we're building dynamic libraries.
ifeq ($(BUILD_SHARED_LIBS), 1)
  GLOBAL_CFLAGS += -fPIC
  GLOBAL_CXXFLAGS += -fPIC
  GLOBAL_LDFLAGS += -Wl,--no-copy-dt-needed-entries
endif


# Pass -g if we want debug info.
ifeq ($(BUILD_DEBUG), 1)
  GLOBAL_CFLAGS += -g
  GLOBAL_CXXFLAGS += -g
endif


include mk/clean.mk
include mk/dist.mk
include mk/install.mk
include mk/libraries.mk
include mk/programs.mk
include mk/patterns.mk


# Include all sub-Makefiles.
define include-sub-makefile =
  d := $$(patsubst %/, %, $$(dir $(1)))
  include $(1)
endef

$(foreach mf, $(SUBS), $(eval $(call include-sub-makefile, $(mf))))


# Instantiate libraries and programs.
$(foreach lib, $(LIBS), $(eval $(call build-library,$(lib))))
$(foreach prog, $(PROGRAMS), $(eval $(call build-program,$(prog))))


all: $(programs_list)
