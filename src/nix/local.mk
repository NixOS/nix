programs += nix

nix_DIR := $(d)
nix_RELDIR := $(reldir)

nix_SOURCES := $(subst $(d)/,,$(wildcard $(d)/*.cc))
nix_SOURCES += ../linenoise/linenoise.c

nix_LIBS = libexpr libmain libstore libutil libformat

$(eval $(call install-symlink, nix, $(bindir)/nix-hash))
