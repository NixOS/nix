default: all


# Get rid of default suffixes. FIXME: is this a good idea?
.SUFFIXES:


# Initialise some variables.
bin-scripts :=
noinst-scripts :=
man-pages :=
install-tests :=

ifdef HOST_OS
  HOST_KERNEL = $(firstword $(subst -, ,$(HOST_OS)))
  ifeq ($(HOST_KERNEL), cygwin)
    HOST_CYGWIN = 1
  endif
  ifeq ($(patsubst darwin%,,$(HOST_KERNEL)),)
    HOST_DARWIN = 1
  endif
  ifeq ($(patsubst freebsd%,,$(HOST_KERNEL)),)
    HOST_FREEBSD = 1
  endif
  ifeq ($(HOST_KERNEL), linux)
    HOST_LINUX = 1
  endif
  ifeq ($(patsubst solaris%,,$(HOST_KERNEL)),)
    HOST_SOLARIS = 1
  endif
endif

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


include mk/functions.mk
include mk/tracing.mk
include mk/clean.mk
include mk/install.mk
include mk/libraries.mk
include mk/programs.mk
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
$(foreach script, $(bin-scripts), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin-scripts), $(eval programs-list += $(script)))
$(foreach script, $(noinst-scripts), $(eval programs-list += $(script)))
$(foreach template, $(template-files), $(eval $(call instantiate-template,$(template))))
$(foreach test, $(install-tests), $(eval $(call run-install-test,$(test))))
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
