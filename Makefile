include mk/build-dir.mk

-include $(buildprefix)Makefile.config
clean-files += $(buildprefix)Makefile.config

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
  tests/unit/libexpr/local.mk \
  tests/unit/libexpr-support/local.mk
endif

ifeq ($(ENABLE_FUNCTIONAL_TESTS), yes)
makefiles += \
  tests/functional/local.mk \
  tests/functional/ca/local.mk \
  tests/functional/dyn-drv/local.mk \
  tests/functional/test-libstoreconsumer/local.mk \
  tests/functional/plugins/local.mk
endif

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CXXFLAGS += -O3 $(CXXLTO)
  GLOBAL_LDFLAGS += $(CXXLTO)
else
  GLOBAL_CXXFLAGS += -O0 -U_FORTIFY_SOURCE
endif

include mk/lib.mk

# Must be included after `mk/lib.mk` so isn't the default target.
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

# Must be included after `mk/lib.mk` so rules refer to variables defined
# by the library. Rules are not "lazy" like variables, unfortunately.

ifeq ($(ENABLE_DOC_GEN), yes)
$(eval $(call include-sub-makefile, doc/manual/local.mk))
else
.PHONY: manual-html manpages
manual-html manpages:
	@echo "Generated docs are disabled. Configure without '--disable-doc-gen', or avoid calling 'make manpages' and 'make manual-html'."
	@exit 1
endif

ifeq ($(ENABLE_INTERNAL_API_DOCS), yes)
$(eval $(call include-sub-makefile, doc/internal-api/local.mk))
else
.PHONY: internal-api-html
internal-api-html:
	@echo "Internal API docs are disabled. Configure with '--enable-internal-api-docs', or avoid calling 'make internal-api-html'."
	@exit 1
endif

GLOBAL_CXXFLAGS += -g -Wall -include $(buildprefix)config.h -std=c++2a -I src
