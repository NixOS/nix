programs += nix

nix_DIR := $(d)
nix_RELDIR := $(reldir)

nix_SOURCES := $(wildcard $(d)/*.cc) $(TOP)/src/linenoise/linenoise.c

nix_LIBS = libexpr libmain libstore libutil libformat

$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
