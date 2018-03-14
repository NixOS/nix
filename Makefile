makefiles = \
  local.mk \
  src/libutil/local.mk \
  src/libstore/local.mk \
  src/libmain/local.mk \
  src/libexpr/local.mk \
  src/nix/local.mk \
  src/nix-store/local.mk \
  src/nix-instantiate/local.mk \
  src/nix-env/local.mk \
  src/nix-daemon/local.mk \
  src/nix-collect-garbage/local.mk \
  src/nix-copy-closure/local.mk \
  src/nix-prefetch-url/local.mk \
  src/buildenv/local.mk \
  src/resolve-system-dependencies/local.mk \
  src/nix-channel/local.mk \
  src/nix-build/local.mk \
  src/build-remote/local.mk \
  scripts/local.mk \
  corepkgs/local.mk \
  misc/systemd/local.mk \
  misc/launchd/local.mk \
  misc/upstart/local.mk \
  doc/manual/local.mk \
  tests/local.mk \
  tests/plugins/local.mk

GLOBAL_CXXFLAGS += -std=c++14 -g -Wall -include config.h

-include Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CFLAGS += -O3
  GLOBAL_CXXFLAGS += -O3
endif

include mk/lib.mk
