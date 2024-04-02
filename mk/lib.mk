default: all


# Get rid of default suffixes. FIXME: is this a good idea?
.SUFFIXES:


# Initialise some variables.
bin-scripts :=
noinst-scripts :=
man-pages :=
install-tests :=
install-tests-groups :=

include mk/platform.mk

# Hack to define a literal space.
space :=
space +=


# Hack to define a literal newline.
define newline


endef


# Pass -fPIC if we're building dynamic libraries.
BUILD_SHARED_LIBS ?= 1

ifeq ($(BUILD_SHARED_LIBS), 1)
  ifdef HOST_CYGWIN
    GLOBAL_CFLAGS += -U__STRICT_ANSI__ -D_GNU_SOURCE
    GLOBAL_CXXFLAGS += -U__STRICT_ANSI__ -D_GNU_SOURCE
  else
    GLOBAL_CFLAGS += -fPIC
    GLOBAL_CXXFLAGS += -fPIC
  endif
  ifndef HOST_DARWIN
   ifndef HOST_SOLARIS
    ifndef HOST_FREEBSD
     GLOBAL_LDFLAGS += -Wl,--no-copy-dt-needed-entries
    endif
   endif
  endif
  SET_RPATH_TO_LIBS ?= 1
endif

# Pass -g if we want debug info.
BUILD_DEBUG ?= 1

ifeq ($(BUILD_DEBUG), 1)
  GLOBAL_CFLAGS += -g
  GLOBAL_CXXFLAGS += -g
endif


include mk/build-dir.mk
include mk/install-dirs.mk
include mk/functions.mk
include mk/tracing.mk
include mk/clean.mk
include mk/install.mk
include mk/libraries.mk
include mk/programs.mk
include mk/patterns.mk
include mk/templates.mk
include mk/cxx-big-literal.mk
include mk/tests.mk
include mk/compilation-database.mk


# Include all sub-Makefiles.
define include-sub-makefile
  d := $$(patsubst %/,%,$$(dir $(1)))
  include $(1)
endef

$(foreach mf, $(makefiles), $(eval $(call include-sub-makefile,$(mf))))


# Instantiate stuff.
$(foreach lib, $(libraries), $(eval $(call build-library,$(lib))))
$(foreach prog, $(programs), $(eval $(call build-program,$(prog))))
$(foreach script, $(bin-scripts), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin-scripts), $(eval programs-list += $(script)))
$(foreach script, $(noinst-scripts), $(eval programs-list += $(script)))
$(foreach template, $(template-files), $(eval $(call instantiate-template,$(template))))
install_test_init=tests/functional/init.sh
$(foreach test, $(install-tests), \
  $(eval $(call run-test,$(test),$(install_test_init))) \
  $(eval installcheck: $(test).test))
$(foreach test-group, $(install-tests-groups), \
  $(eval $(call run-test-group,$(test-group),$(install_test_init))) \
  $(eval installcheck: $(test-group).test-group) \
  $(foreach test, $($(test-group)-tests), \
    $(eval $(call run-test,$(test),$(install_test_init))) \
    $(eval $(test-group).test-group: $(test).test)))

# Compilation database.
$(foreach lib, $(libraries), $(eval $(call write-compile-commands,$(lib))))
$(foreach prog, $(programs), $(eval $(call write-compile-commands,$(prog))))

compile_commands.json: $(compile-commands-json-files)
	@jq --slurp '.' $^ >$@

# Include makefiles requiring built programs.
$(foreach mf, $(makefiles-late), $(eval $(call include-sub-makefile,$(mf))))


$(foreach file, $(man-pages), $(eval $(call install-data-in, $(file), $(mandir)/man$(patsubst .%,%,$(suffix $(file))))))


.PHONY: default all man help

all: $(programs-list) $(libs-list) $(man-pages)

man: $(man-pages)


help:
	@echo "The following targets are available:"
	@echo ""
	@echo "  default: Build default targets"
ifdef man-pages
	@echo "  man: Generate manual pages"
endif
	@$(print-top-help)
ifdef programs-list
	@echo ""
	@echo "The following programs can be built:"
	@echo ""
	@for i in $(programs-list); do echo "  $$i"; done
endif
ifdef libs-list
	@echo ""
	@echo "The following libraries can be built:"
	@echo ""
	@for i in $(libs-list); do echo "  $$i"; done
endif
ifdef install-tests-groups
	@echo ""
	@echo "The following groups of functional tests can be run:"
	@echo ""
	@for i in $(install-tests-groups); do echo "  $$i.test-group"; done
	@echo ""
	@echo "(installcheck includes tests in test groups too.)"
endif
	@echo ""
	@echo "The following variables control the build:"
	@echo ""
	@echo "  BUILD_SHARED_LIBS ($(BUILD_SHARED_LIBS)): Whether to build shared libraries"
	@echo "  BUILD_DEBUG ($(BUILD_DEBUG)): Whether to include debug symbols"
	@echo "  CC ($(CC)): C compiler to be used"
	@echo "  CFLAGS: Flags for the C compiler"
	@echo "  CXX ($(CXX)): C++ compiler to be used"
	@echo "  CXXFLAGS: Flags for the C++ compiler"
	@echo "  CPPFLAGS: C preprocessor flags, used for both CC and CXX"
	@$(print-var-help)
