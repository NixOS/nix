default: all


# Get rid of default suffixes. FIXME: is this a good idea?
.SUFFIXES:


# Initialise some variables.
QUIET = @
bin_SCRIPTS :=
noinst_SCRIPTS :=
OS = $(shell uname -s)


# Default installation paths.
prefix ?= /usr/local
libdir ?= $(prefix)/lib
bindir ?= $(prefix)/bin
libexecdir ?= $(prefix)/libexec
datadir ?= $(prefix)/share
localstatedir ?= $(prefix)/var
sysconfdir ?= $(prefix)/etc


# Pass -fPIC if we're building dynamic libraries.
BUILD_SHARED_LIBS ?= 1

ifeq ($(BUILD_SHARED_LIBS), 1)
  GLOBAL_CFLAGS += -fPIC
  GLOBAL_CXXFLAGS += -fPIC
  ifneq ($(OS), Darwin)
    GLOBAL_LDFLAGS += -Wl,--no-copy-dt-needed-entries
  endif
endif


# Pass -g if we want debug info.
BUILD_DEBUG ?= 1

ifeq ($(BUILD_DEBUG), 1)
  GLOBAL_CFLAGS += -g
  GLOBAL_CXXFLAGS += -g
  GLOBAL_JAVACFLAGS += -g
endif


# Utility function for recursively finding files, e.g.
# ‘$(call rwildcard, path/to/dir, *.c *.h)’.
rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))


include mk/clean.mk
include mk/dist.mk
include mk/install.mk
include mk/libraries.mk
include mk/programs.mk
include mk/jars.mk
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
$(foreach jar, $(JARS), $(eval $(call build-jar,$(jar))))
$(foreach script, $(bin_SCRIPTS), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin_SCRIPTS), $(eval programs_list += $(script)))
$(foreach script, $(noinst_SCRIPTS), $(eval programs_list += $(script)))
$(foreach template, $(template_files), $(eval $(call instantiate-template,$(template))))
$(foreach test, $(INSTALL_TESTS), $(eval $(call run-install-test,$(test))))


all: $(programs_list) $(libs_list) $(jars_list)


help:
	@echo "The following targets are available:"
	@echo ""
	@echo "  default: Build default targets"
	@echo "  install: Install into \$$(prefix) (currently set to '$(prefix)')"
	@echo "  clean: Delete generated files"
	@echo "  dryclean: Show what files would be deleted by 'make clean'"
ifdef PACKAGE_NAME
	@echo "  dist: Generate a source distribution"
endif
ifdef programs_list
	@echo ""
	@echo "The following programs can be built:"
	@echo ""
	@for i in $(programs_list); do echo "  $$i"; done
endif
ifdef libs_list
	@echo ""
	@echo "The following libraries can be built:"
	@echo ""
	@for i in $(libs_list); do echo "  $$i"; done
endif
ifdef jars_list
	@echo ""
	@echo "The following JARs can be built:"
	@echo ""
	@for i in $(jars_list); do echo "  $$i"; done
endif
