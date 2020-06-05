programs += nix

nix_DIR := $(d)

nix_SOURCES := $(wildcard $(d)/*.cc) src/linenoise/linenoise.c

nix_LIBS = libexpr libmain libstore libutil libformat

$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
