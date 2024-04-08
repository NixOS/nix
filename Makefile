# External build directory support

include mk/build-dir.mk

-include $(buildprefix)Makefile.config
clean-files += $(buildprefix)Makefile.config

# List makefiles

ifeq ($(ENABLE_BUILD), yes)
makefiles = \
  mk/precompiled-headers.mk \
  local.mk \
  src/libutil/local.mk \
  src/libstore/local.mk \
  src/libfetchers/local.mk \
  src/libmain/local.mk \
  src/libexpr/local.mk \
  src/libcmd/local.mk \
  src/nix/local.mk \
  src/libutil-c/local.mk \
  src/libstore-c/local.mk \
  src/libexpr-c/local.mk \
  src/resolve-system-dependencies/local.mk \
  scripts/local.mk \
  misc/bash/local.mk \
  misc/fish/local.mk \
  misc/zsh/local.mk \
  misc/systemd/local.mk \
  misc/launchd/local.mk \
  misc/upstart/local.mk
endif

ifeq ($(ENABLE_UNIT_TESTS), yes)
makefiles += \
  tests/unit/libutil/local.mk \
  tests/unit/libutil-support/local.mk \
  tests/unit/libstore/local.mk \
  tests/unit/libstore-support/local.mk \
  tests/unit/libfetchers/local.mk \
  tests/unit/libexpr/local.mk \
  tests/unit/libexpr-support/local.mk
endif

ifeq ($(ENABLE_FUNCTIONAL_TESTS), yes)
makefiles += \
  tests/functional/local.mk \
  tests/functional/ca/local.mk \
  tests/functional/git-hashing/local.mk \
  tests/functional/dyn-drv/local.mk \
  tests/functional/local-overlay-store/local.mk \
  tests/functional/test-libstoreconsumer/local.mk \
  tests/functional/plugins/local.mk
endif

# Some makefiles require access to built programs and must be included late.
makefiles-late =

ifeq ($(ENABLE_DOC_GEN), yes)
makefiles-late += doc/manual/local.mk
endif

ifeq ($(ENABLE_INTERNAL_API_DOCS), yes)
makefiles-late += doc/internal-api/local.mk
endif

ifeq ($(ENABLE_EXTERNAL_API_DOCS), yes)
makefiles-late += doc/external-api/local.mk
endif

# Miscellaneous global Flags

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CXXFLAGS += -O3 $(CXXLTO)
  GLOBAL_LDFLAGS += $(CXXLTO)
else
  GLOBAL_CXXFLAGS += -O0 -U_FORTIFY_SOURCE
  unexport NIX_HARDENING_ENABLE
endif

include mk/platform.mk

ifdef HOST_WINDOWS
  # Windows DLLs are stricter about symbol visibility than Unix shared
  # objects --- see https://gcc.gnu.org/wiki/Visibility for details.
  # This is a temporary sledgehammer to export everything like on Unix,
  # and not detail with this yet.
  #
  # TODO do not do this, and instead do fine-grained export annotations.
  GLOBAL_LDFLAGS += -Wl,--export-all-symbols
endif

GLOBAL_CXXFLAGS += -g -Wall -Wimplicit-fallthrough -include $(buildprefix)config.h -std=c++2a -I src

# Include the main lib, causing rules to be defined

include mk/lib.mk

# Fallback stub rules for better UX when things are disabled
#
# These must be defined after `mk/lib.mk`. Otherwise the first rule
# incorrectly becomes the default target.

ifneq ($(ENABLE_UNIT_TESTS), yes)
.PHONY: check
check:
	@echo "Unit tests are disabled. Configure without '--disable-unit-tests', or avoid calling 'make check'."
	@exit 1
endif

ifneq ($(ENABLE_FUNCTIONAL_TESTS), yes)
.PHONY: installcheck
installcheck:
	@echo "Functional tests are disabled. Configure without '--disable-functional-tests', or avoid calling 'make installcheck'."
	@exit 1
endif

# Documentation fallback stub rules.

ifneq ($(ENABLE_DOC_GEN), yes)
.PHONY: manual-html manpages
manual-html manpages:
	@echo "Generated docs are disabled. Configure without '--disable-doc-gen', or avoid calling 'make manpages' and 'make manual-html'."
	@exit 1
endif

ifneq ($(ENABLE_INTERNAL_API_DOCS), yes)
.PHONY: internal-api-html
internal-api-html:
	@echo "Internal API docs are disabled. Configure with '--enable-internal-api-docs', or avoid calling 'make internal-api-html'."
	@exit 1
endif

ifneq ($(ENABLE_EXTERNAL_API_DOCS), yes)
.PHONY: external-api-html
external-api-html:
	@echo "External API docs are disabled. Configure with '--enable-external-api-docs', or avoid calling 'make external-api-html'."
	@exit 1
endif
