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

ifeq ($(ENABLE_BUILD)_$(ENABLE_TESTS), yes_yes)
makefiles += \
  tests/unit/libutil/local.mk \
  tests/unit/libutil-support/local.mk \
  tests/unit/libstore/local.mk \
  tests/unit/libstore-support/local.mk \
  tests/unit/libexpr/local.mk \
  tests/unit/libexpr-support/local.mk
endif

ifeq ($(ENABLE_TESTS), yes)
makefiles += \
  tests/functional/local.mk \
  tests/functional/ca/local.mk \
  tests/functional/dyn-drv/local.mk \
  tests/functional/test-libstoreconsumer/local.mk \
  tests/functional/plugins/local.mk
else
makefiles += \
  mk/disable-tests.mk
endif

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CXXFLAGS += -O3 $(CXXLTO)
  GLOBAL_LDFLAGS += $(CXXLTO)
else
  GLOBAL_CXXFLAGS += -O0 -U_FORTIFY_SOURCE
endif

include mk/lib.mk

# Must be included after `mk/lib.mk` so rules refer to variables defined
# by the library. Rules are not "lazy" like variables, unfortunately.
ifeq ($(ENABLE_BUILD), yes)
$(eval $(call include-sub-makefile, doc/manual/local.mk))
$(eval $(call include-sub-makefile, doc/internal-api/local.mk))
endif

GLOBAL_CXXFLAGS += -g -Wall -include $(buildprefix)config.h -std=c++2a -I src
