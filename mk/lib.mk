default: all


# Include Autoconf variables.
template_files += Makefile.config
include Makefile.config


# Get rid of default suffixes. FIXME: is this a good idea?
.SUFFIXES:


# Initialise some variables.
QUIET = @
bin_SCRIPTS :=
noinst_SCRIPTS :=


# Pass -fPIC if we're building dynamic libraries.
BUILD_SHARED_LIBS = 1

ifeq ($(BUILD_SHARED_LIBS), 1)
  GLOBAL_CFLAGS += -fPIC
  GLOBAL_CXXFLAGS += -fPIC
  GLOBAL_LDFLAGS += -Wl,--no-copy-dt-needed-entries
endif


# Pass -g if we want debug info.
BUILD_DEBUG = 1

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
include mk/templates.mk
include mk/tests.mk


# Include all sub-Makefiles.
define include-sub-makefile =
  d := $$(patsubst %/,%,$$(dir $(1)))
  include $(1)
endef

$(foreach mf, $(SUBS), $(eval $(call include-sub-makefile, $(mf))))


# Instantiate stuff.
$(foreach lib, $(LIBS), $(eval $(call build-library,$(lib))))
$(foreach prog, $(PROGRAMS), $(eval $(call build-program,$(prog))))
$(foreach script, $(bin_SCRIPTS), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin_SCRIPTS), $(eval programs_list += $(script)))
$(foreach script, $(noinst_SCRIPTS), $(eval programs_list += $(script)))
$(foreach template, $(template_files), $(eval $(call instantiate-template,$(template))))
$(foreach test, $(INSTALL_TESTS), $(eval $(call run-install-test,$(test))))


all: $(programs_list) $(libs_list)
