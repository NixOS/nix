makefiles = \
  local.mk \
  src/boost/format/local.mk \
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
  src/nix-prefetch-url/local.mk \
  src/buildenv/local.mk \
  src/resolve-system-dependencies/local.mk \
  perl/local.mk \
  scripts/local.mk \
  corepkgs/local.mk \
  misc/systemd/local.mk \
  misc/launchd/local.mk \
  misc/upstart/local.mk \
  misc/emacs/local.mk \
  doc/manual/local.mk \
  tests/local.mk
  #src/download-via-ssh/local.mk \

GLOBAL_CXXFLAGS += -std=c++11 -g -Wall

-include Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CFLAGS += -O3
  GLOBAL_CXXFLAGS += -O3
endif

include mk/lib.mk
