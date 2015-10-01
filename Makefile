makefiles = \
  local.mk \
  src/boost/format/local.mk \
  src/libutil/local.mk \
  src/libstore/local.mk \
  src/libmain/local.mk \
  src/libexpr/local.mk \
  src/nix-hash/local.mk \
  src/nix-store/local.mk \
  src/nix-instantiate/local.mk \
  src/nix-env/local.mk \
  src/nix-daemon/local.mk \
  src/nix-collect-garbage/local.mk \
  src/download-via-ssh/local.mk \
  src/nix-log2xml/local.mk \
  src/nix-prefetch-url/local.mk \
  src/bsdiff-4.3/local.mk \
  perl/local.mk \
  scripts/local.mk \
  corepkgs/local.mk \
  misc/systemd/local.mk \
  misc/launchd/local.mk \
  misc/upstart/local.mk \
  misc/emacs/local.mk \
  doc/manual/local.mk \
  tests/local.mk

GLOBAL_CXXFLAGS += -std=c++0x -g -Wall

-include Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CFLAGS += -O3
  GLOBAL_CXXFLAGS += -O3
endif

include mk/lib.mk
