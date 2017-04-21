default: all


# Get rid of default suffixes. FIXME: is this a good idea?
.SUFFIXES:


# Initialise some variables.
bin-scripts :=
noinst-scripts :=
man-pages :=
install-tests :=
dist-files :=
OS = $(shell uname -s)


# Hack to define a literal space.
space :=
space +=


# Hack to define a literal newline.
define newline


endef


# Default installation paths.
prefix ?= /usr/local
libdir ?= $(prefix)/lib
bindir ?= $(prefix)/bin
libexecdir ?= $(prefix)/libexec
datadir ?= $(prefix)/share
jardir ?= $(datadir)/java
localstatedir ?= $(prefix)/var
sysconfdir ?= $(prefix)/etc
mandir ?= $(prefix)/share/man


# Initialise support for build directories.
builddir ?=

ifdef builddir
  buildprefix = $(builddir)/
else
  buildprefix =
endif


# Pass -fPIC if we're building dynamic libraries.
BUILD_SHARED_LIBS ?= 1

ifeq ($(BUILD_SHARED_LIBS), 1)
  ifeq (CYGWIN,$(findstring CYGWIN,$(OS)))
    GLOBAL_CFLAGS += -U__STRICT_ANSI__ -D_GNU_SOURCE
    GLOBAL_CXXFLAGS += -U__STRICT_ANSI__ -D_GNU_SOURCE
  else
    GLOBAL_CFLAGS += -fPIC
    GLOBAL_CXXFLAGS += -fPIC
  endif
  ifneq ($(OS), Darwin)
   ifneq ($(OS), SunOS)
    ifneq ($(OS), FreeBSD)
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
  GLOBAL_JAVACFLAGS += -g
endif


include mk/functions.mk
include mk/tracing.mk
include mk/clean.mk
include mk/install.mk
include mk/libraries.mk
include mk/programs.mk
include mk/jars.mk
include mk/patterns.mk
include mk/templates.mk
include mk/tests.mk


# Include all sub-Makefiles.
define include-sub-makefile
  d := $$(patsubst %/,%,$$(dir $(1)))
  include $(1)
endef

$(foreach mf, $(makefiles), $(eval $(call include-sub-makefile, $(mf))))


# Instantiate stuff.
$(foreach lib, $(libraries), $(eval $(call build-library,$(lib))))
$(foreach prog, $(programs), $(eval $(call build-program,$(prog))))
$(foreach jar, $(jars), $(eval $(call build-jar,$(jar))))
$(foreach script, $(bin-scripts), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin-scripts), $(eval programs-list += $(script)))
$(foreach script, $(noinst-scripts), $(eval programs-list += $(script)))
$(foreach template, $(template-files), $(eval $(call instantiate-template,$(template))))
$(foreach test, $(install-tests), $(eval $(call run-install-test,$(test))))
$(foreach file, $(man-pages), $(eval $(call install-data-in, $(file), $(mandir)/man$(patsubst .%,%,$(suffix $(file))))))


include mk/dist.mk


.PHONY: default all man help

all: $(programs-list) $(libs-list) $(jars-list) $(man-pages)

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
ifdef jars-list
	@echo ""
	@echo "The following JARs can be built:"
	@echo ""
	@for i in $(jars-list); do echo "  $$i"; done
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
	@$(print-var-help)
