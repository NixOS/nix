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
  buildprefix = $(builddir)
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


include $(TOP)/mk/functions.mk
include $(TOP)/mk/tracing.mk
include $(TOP)/mk/clean.mk
include $(TOP)/mk/install.mk
include $(TOP)/mk/libraries.mk
include $(TOP)/mk/programs.mk
include $(TOP)/mk/jars.mk
include $(TOP)/mk/patterns.mk
include $(TOP)/mk/templates.mk
include $(TOP)/mk/tests.mk


# Include all sub-Makefiles.
define include-sub-makefile
d := $$(patsubst %/,%,$(TOP)/$$(dir $(1)))
reldir := $$(patsubst %/,%,$$(dir $(1)))
#$(1)_DIR := $(d)
#$(1)_RELDIR := $(reldir)
include $(TOP)/$(1)
undefine d
undefine reldir
endef

$(foreach mf, $(makefiles), $(eval $(call include-sub-makefile,$(mf))))

define print-target-vars
$(1)_NAME    = $($(1)_NAME)
$(1)_DIR     = $($(1)_DIR)
$(1)_RELDIR  = $($(1)_RELDIR)
$(1)_OUT     = $($(1)_OUT)
$(1)_OBJS    = $($(1)_OBJS)

endef

# Instantiate stuff.
$(foreach lib, $(libraries), $(eval $(call build-library,$(lib))))
#$(foreach lib, $(libraries), $(info $(call print-target-vars,$(lib))))

$(foreach prog, $(programs), $(eval $(call build-program,$(prog))))
#$(foreach prog, $(programs), $(info $(call print-target-vars,$(prog))))

$(foreach jar, $(jars), $(eval $(call build-jar,$(jar))))
$(foreach script, $(bin-scripts), $(eval $(call install-program-in,$(script),$(bindir))))
$(foreach script, $(bin-scripts), $(eval programs-list += $(script)))
$(foreach script, $(noinst-scripts), $(eval programs-list += $(script)))
$(foreach template, $(template-files), $(eval $(call instantiate-template,$(template))))
$(foreach test, $(install-tests), $(eval $(call run-install-test,$(test))))
$(foreach file, $(man-pages), $(eval $(call install-data-in, $(file), $(mandir)/man$(patsubst .%,%,$(suffix $(file))))))


include $(TOP)/mk/dist.mk


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
	@for i in $(programs-list-no-path); do echo "  $$i"; done
endif
ifdef libs-list
	@echo ""
	@echo "The following libraries can be built:"
	@echo ""
	@for i in $(libs-list-no-path); do echo "  $$i"; done
endif
ifdef jars-list
	@echo ""
	@echo "The following JARs can be built:"
	@echo ""
	@for i in $(jars-list-no-path); do echo "  $$i"; done
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
