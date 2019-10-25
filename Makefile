makefiles = \
  local.mk \
  src/libutil/local.mk \
  src/libstore/local.mk \
  src/libmain/local.mk \
  src/libexpr/local.mk \
  src/nix/local.mk \
  src/resolve-system-dependencies/local.mk \
  scripts/local.mk \
  corepkgs/local.mk \
  misc/systemd/local.mk \
  misc/launchd/local.mk \
  misc/upstart/local.mk \
  doc/manual/local.mk \
  tests/local.mk \
  tests/plugins/local.mk

GLOBAL_CXXFLAGS += -g -Wall -include config.h

-include Makefile.config

include mk/lib.mk
