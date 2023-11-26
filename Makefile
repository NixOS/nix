-include Makefile.config
clean-files += Makefile.config

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
  misc/upstart/local.mk \
  doc/manual/local.mk \
  doc/internal-api/local.mk
endif

ifeq ($(ENABLE_BUILD)_$(ENABLE_TESTS), yes_yes)
UNIT_TEST_ENV = _NIX_TEST_UNIT_DATA=unit-test-data
makefiles += \
  src/libutil/tests/local.mk \
  src/libstore/tests/local.mk \
  src/libexpr/tests/local.mk
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

GLOBAL_CXXFLAGS += -g -Wall -include config.h -std=c++2a -I src
