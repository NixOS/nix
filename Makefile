TOP := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

makefiles += local.mk
makefiles += src/boost/format/local.mk
makefiles += src/libutil/local.mk
makefiles += src/libstore/local.mk
makefiles += src/libmain/local.mk
makefiles += src/libexpr/local.mk
makefiles += src/nix/local.mk
makefiles += src/nix-store/local.mk
makefiles += src/nix-instantiate/local.mk
makefiles += src/nix-env/local.mk
makefiles += src/nix-daemon/local.mk
makefiles += src/nix-collect-garbage/local.mk
makefiles += src/nix-copy-closure/local.mk
makefiles += src/nix-prefetch-url/local.mk
makefiles += src/buildenv/local.mk
makefiles += src/resolve-system-dependencies/local.mk
makefiles += src/nix-channel/local.mk
makefiles += src/nix-build/local.mk
makefiles += src/build-remote/local.mk
makefiles += scripts/local.mk
makefiles += corepkgs/local.mk
makefiles += misc/systemd/local.mk
makefiles += misc/launchd/local.mk
makefiles += misc/upstart/local.mk
makefiles += misc/emacs/local.mk
makefiles += doc/manual/local.mk
makefiles += tests/local.mk

GLOBAL_CXXFLAGS += -std=c++14 -g -Wall -include config.h

-include $(TOP)/Makefile.config

OPTIMIZE = 1

ifeq ($(OPTIMIZE), 1)
  GLOBAL_CFLAGS += -O3
  GLOBAL_CXXFLAGS += -O3
endif

include $(TOP)/mk/lib.mk
